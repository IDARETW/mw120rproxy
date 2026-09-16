#include "write_xsurface.h"
#include "../../common/log.h"
#include "../convert/conv_xsurface.h"
#include "../dumpsrc/xse_dump.h"
#include "iw8_focus_structs.h"
#include "iw8_structs.h"
#include "iw8_zone.h"
#include <cstring>

namespace iw8xs_dump
{

using namespace iw8;
namespace fx = iw8_focus;

// Byte offsets inside the pinned IW8 structs (from iw8_focus_structs.h) — used to stamp the raw
// tagged-pointer sentinels regardless of the C++ pointer field type.
namespace off
{
// XModelSurfs (0x60)
constexpr size_t XMS_name = 0x00;
constexpr size_t XMS_surfs = 0x08;
constexpr size_t XMS_shared = 0x30;
// XSurface (0xC0)
constexpr size_t XS_shared = 0x48;
constexpr size_t XS_lmap = 0x50;
constexpr size_t XS_rigid = 0x58;
constexpr size_t XS_blend = 0x60;
constexpr size_t XS_subdiv = 0x68;
constexpr size_t XS_childB = 0xA8;
constexpr size_t XS_bsPerVert = 0xB0;
constexpr size_t XS_bsRecalc = 0xB8;
// XSurfaceShared (0x10)
constexpr size_t SH_data = 0x00;
} // namespace off

static inline void stamp(void *base, size_t at, uint64_t sentinel)
{
    std::memcpy(static_cast<uint8_t *>(base) + at, &sentinel, 8);
}

// Emit the FULL XModelSurfs BODY (struct + surfs[] + shared blob) for an already-converted
// Iw8Surfs. Body-callback content only (run deferred inside zw.build()); see
// writeXModelSurfsFromDump.
static void emitFullBody(ZoneWriter &zw, const std::string &name, const conv_xsurf::Iw8Surfs &cv)
{
    const uint16_t numsurfs = (uint16_t)cv.surfaces.size();
    const uint32_t sharedDataSize = (uint32_t)cv.sharedBlob.size();
    const uint64_t surfaceOffset =
        (zw.buffer().streamSize(XFILE_BLOCK_VIRTUAL) + name.size() + 1 + 15) & ~15ull;
    const uint64_t sharedOffset = surfaceOffset + numsurfs * sizeof(fx::XSurface);
    if (sharedOffset >= UINT32_MAX)
        throw std::runtime_error("Model shared pointer exceeds Replay's packed range");
    const uint64_t sharedPointer = (uint64_t(XFILE_BLOCK_VIRTUAL) << 32) | (sharedOffset + 1);

    // ---- 3) XModelSurfs struct -> TEMP_PRELOAD(1)
    // ------------------------------------------------
    fx::XModelSurfs xs{};
    std::memset(&xs, 0, sizeof(xs));
    stamp(&xs, off::XMS_name, PTR_FOLLOWS);  // name follows
    stamp(&xs, off::XMS_surfs, PTR_FOLLOWS); // surfs follow
    // xpakEntry (0x10..0x2F) stays zero — geometry is inline, not streamed via UGB.
    stamp(&xs, off::XMS_shared, sharedPointer);
    xs.numsurfs = numsurfs;
    xs.ugbState = 0;
    std::memcpy(reinterpret_cast<uint8_t *>(&xs) + 0x3C, cv.partBits, 32); // partBits[32] @0x3C

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);
    zw.write(&xs, sizeof(xs)); // 0x60 bytes

    // ---- 4) trailing data -> VIRTUAL(8)
    // ----------------------------------------------------------
    zw.pushStream(XFILE_BLOCK_VIRTUAL);

    // 4a) name (XModelSurfs.name = -2)
    zw.writeStr(name);

    // 4b) surfs[numsurfs]: the 16-byte-aligned XSurface(0xC0) array.
    zw.align(15);
    for (uint16_t i = 0; i < numsurfs; ++i)
    {
        const conv_xsurf::Iw8SurfaceCvt &sc = cv.surfaces[i];
        fx::XSurface su{};
        std::memset(&su, 0, sizeof(su));
        su.flags = sc.flags;
        su.vertCount = sc.vertCount;
        su.triCount = sc.triCount;
        su.blendShapeTargetCount = 0;
        su.rigidVertListCount = static_cast<uint8_t>(sc.rigidVertLists.size());
        su.subdivLevelCount = 0;
        su.ugbID = 0;
        su.hash = conv_xsurf::surfaceHash(cv, sc);
        std::memcpy(su.blendVertCounts, sc.blendVertCounts, sizeof(su.blendVertCounts));
        su.blendVertSize = static_cast<uint32_t>(sc.blendVerts.size() * sizeof(uint16_t));
        su.sharedVertDataOffset = sc.sharedVertDataOffset;
        su.sharedIndexDataOffset = sc.sharedIndexDataOffset;
        su.sharedTriClusterDataOffset = sc.sharedTriClusterDataOffset;
        su.sharedColorDataOffset = sc.sharedColorDataOffset;
        su.sharedSecondUVDataOffset = UINT32_MAX;
        su.sharedNormalTransformDataOffset = UINT32_MAX;
        su.sharedTensionAccumTableOffset = UINT32_MAX;
        su.sharedTensionDataOffset = UINT32_MAX;
        // Load_XSurface reads the first shared descriptor after the surface headers.
        // The remaining surfaces and XModelSurfs share that same resident descriptor.
        stamp(&su, off::XS_shared, i == 0 ? PTR_FOLLOWS : sharedPointer);
        stamp(&su, off::XS_lmap, PTR_NULL);
        stamp(&su, off::XS_rigid, sc.rigidVertLists.empty() ? PTR_NULL : PTR_FOLLOWS);
        stamp(&su, off::XS_blend, sc.blendVerts.empty() ? PTR_NULL : PTR_FOLLOWS);
        stamp(&su, off::XS_subdiv, PTR_NULL);
        stamp(&su, off::XS_childB, PTR_NULL);
        stamp(&su, off::XS_bsPerVert, PTR_NULL);
        stamp(&su, off::XS_bsRecalc, PTR_NULL);
        // surfBounds (0x90, 0x18)
        su.surfBounds.midPoint = {{sc.boundsMid[0], sc.boundsMid[1], sc.boundsMid[2]}};
        su.surfBounds.halfSize = {{sc.boundsHalf[0], sc.boundsHalf[1], sc.boundsHalf[2]}};
        // partBits[32] @0x70 (IW5 6 ints widened in the converter)
        std::memcpy(su.partBits, sc.partBits, 32);
        zw.write(&su, sizeof(su)); // 0xC0 bytes
    }

    // The first surface owns the inline shared descriptor; other references point back to it.
    if (sharedDataSize)
    {
        fx::XSurfaceShared sh{};
        std::memset(&sh, 0, sizeof(sh));
        stamp(&sh, off::SH_data, PTR_FOLLOWS); // data follows
        sh.dataSize = sharedDataSize;
        sh.flags = 0; // 0 = loaded (not streamed/xpak)
        zw.align(7);
        if (zw.buffer().streamSize(XFILE_BLOCK_VIRTUAL) != sharedOffset)
            throw std::runtime_error("Model shared descriptor offset drifted");
        zw.write(&sh, sizeof(sh)); // 0x10 bytes
        zw.align(15);
        zw.write(cv.sharedBlob.data(), cv.sharedBlob.size()); // the packed geometry blob
    }

    // Load_XSurface visits each surface's arrays after its shared-data reference.
    // Only the first shared reference consumes bytes; later references point backward.
    for (const auto &surface : cv.surfaces)
    {
        if (bool(surface.flags & 1) != (surface.sharedColorDataOffset != UINT32_MAX))
            throw std::runtime_error("Invalid model surface identity or color registration: " +
                                     name);
        if (!surface.rigidVertLists.empty())
        {
            zw.align(1);
            zw.write(surface.rigidVertLists.data(),
                     surface.rigidVertLists.size() * sizeof(conv_xsurf::Iw8RigidVertList));
        }
        if (!surface.blendVerts.empty())
        {
            zw.align(3);
            zw.write(surface.blendVerts.data(), surface.blendVerts.size() * sizeof(uint16_t));
        }
    }

    zw.popStream(); // VIRTUAL -> TEMP_PRELOAD
    zw.popStream(); // TEMP_PRELOAD -> outer

    zt::info("iw8 xsurface(dump) '%s': wrote XModelSurfs (%u surf(s), %u verts, %u tris, %u "
             "tri-clusters, shared blob %u bytes; %u rigid-run(s), %u blend-word(s))",
             name.c_str(), numsurfs, cv.totalVerts, cv.totalTris, cv.totalTriClusters,
             sharedDataSize, cv.totalRigidRuns, cv.totalBlendWords);
}

bool writeXModelSurfsFromDump(ZoneWriter &zw, const std::string &dumpDir, const std::string &name)
{
    // Parse now; write the owned result later in the zone's asset load order.
    dumpsrc::XseFile xse = dumpsrc::loadXseFile(dumpDir, name);
    if (!xse.loaded)
        throw std::runtime_error("Could not read model surfaces '" + name + "': " + xse.parseError);

    conv_xsurf::Iw8Surfs cv = conv_xsurf::convert(xse);
    writeXModelSurfs(zw, name, cv);
    return true;
}

void writeXModelSurfs(ZoneWriter &zw, const std::string &name, const conv_xsurf::Iw8Surfs &cv)
{
    if (name.empty() || !cv.ok || cv.surfaces.empty() || cv.surfaces.size() > UINT16_MAX ||
        cv.sharedBlob.empty() || cv.sharedBlob.size() > UINT32_MAX)
        throw std::runtime_error("Invalid converted model surfaces: " + name);
    for (const auto &surface : cv.surfaces)
    {
        const size_t indexBytes = static_cast<size_t>(surface.triCount) * 6;
        const size_t vertexBytes = static_cast<size_t>(surface.vertCount) * 20;
        const size_t clusterBytes = static_cast<size_t>((surface.triCount + 63u) / 64u) * 24;
        auto inRange = [&](uint32_t offset, size_t bytes) {
            return offset <= cv.sharedBlob.size() && bytes <= cv.sharedBlob.size() - offset;
        };
        if (!inRange(surface.sharedIndexDataOffset, indexBytes * 2) ||
            surface.sharedVertDataOffset != surface.sharedIndexDataOffset + indexBytes * 2 ||
            !inRange(surface.sharedVertDataOffset, vertexBytes) ||
            surface.sharedTriClusterDataOffset != surface.sharedVertDataOffset + vertexBytes ||
            !inRange(surface.sharedTriClusterDataOffset, clusterBytes) ||
            (surface.sharedColorDataOffset != UINT32_MAX &&
             !inRange(surface.sharedColorDataOffset,
                      static_cast<size_t>(surface.vertCount) * sizeof(uint32_t))))
            throw std::runtime_error("Invalid converted model surface stream layout: " + name);
    }
    zw.add(ASSET_TYPE_XMODELSURFS, name, [name, cv](ZoneWriter &w) { emitFullBody(w, name, cv); });
}

} // namespace iw8xs_dump
