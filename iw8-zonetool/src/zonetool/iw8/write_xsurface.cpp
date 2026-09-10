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

// Emit a minimal, load-safe name-only XModelSurfs BODY (missing/corrupt .xse, or 0 surfaces). This
// is the body callback content only; the XAsset entry framing is done by ZoneWriter::build() once
// the caller registers it via zw.add (see writeXModelSurfsFromDump).
static void emitMinimalBody(ZoneWriter &zw, const std::string &name)
{
    fx::XModelSurfs xs{};
    std::memset(&xs, 0, sizeof(xs));
    stamp(&xs, off::XMS_name, PTR_FOLLOWS); // name follows
    stamp(&xs, off::XMS_surfs, PTR_NULL);   // no surfs
    stamp(&xs, off::XMS_shared, PTR_NULL);  // no shared blob
    xs.numsurfs = 0;

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);
    zw.write(&xs, sizeof(xs));
    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    zw.writeStr(name);
    zw.popStream();
    zw.popStream();
    zt::warn("iw8 xsurface(dump) '%s': emitted minimal name-only XModelSurfs (no geometry)",
             name.c_str());
}

// Emit the FULL XModelSurfs BODY (struct + surfs[] + shared blob) for an already-converted
// Iw8Surfs. Body-callback content only (run deferred inside zw.build()); see
// writeXModelSurfsFromDump.
static void emitFullBody(ZoneWriter &zw, const std::string &name, const conv_xsurf::Iw8Surfs &cv)
{
    const uint16_t numsurfs = (uint16_t)cv.surfaces.size();
    const uint32_t sharedDataSize = (uint32_t)cv.sharedBlob.size();

    // ---- 3) XModelSurfs struct -> TEMP_PRELOAD(1)
    // ------------------------------------------------
    fx::XModelSurfs xs{};
    std::memset(&xs, 0, sizeof(xs));
    stamp(&xs, off::XMS_name, PTR_FOLLOWS);  // name follows
    stamp(&xs, off::XMS_surfs, PTR_FOLLOWS); // surfs follow
    // xpakEntry (0x10..0x2F) stays zero — geometry is inline, not streamed via UGB.
    stamp(&xs, off::XMS_shared, sharedDataSize ? PTR_FOLLOWS : PTR_NULL); // shared follows | null
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
        su.flags = 0;
        su.vertCount = sc.vertCount;
        su.triCount = sc.triCount;
        su.blendShapeTargetCount = 0;
        su.rigidVertListCount = 0; // rigid runs dropped (load-safe; not render-required)
        su.subdivLevelCount = 0;
        su.ugbID = 0;
        su.hash = 0;
        su.blendVertSize = 0;
        su.sharedVertDataOffset = sc.sharedVertDataOffset;
        su.sharedIndexDataOffset = sc.sharedIndexDataOffset;
        su.sharedTriClusterDataOffset = 0;
        su.sharedColorDataOffset = 0;
        su.sharedSecondUVDataOffset = 0;
        su.sharedNormalTransformDataOffset = 0;
        su.sharedTensionAccumTableOffset = 0;
        su.sharedTensionDataOffset = 0;
        // ALL per-surface pointer fields null (the geometry is reached via XModelSurfs.shared, and
        // no per-surface trailing arrays are emitted, so these MUST be 0 for load-safety). su is
        // zero-initialized; we stamp the 8 pointer slots explicitly to be unambiguous.
        stamp(&su, off::XS_shared, PTR_NULL);
        stamp(&su, off::XS_lmap, PTR_NULL);
        stamp(&su, off::XS_rigid, PTR_NULL);
        stamp(&su, off::XS_blend, PTR_NULL);
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

    // 4c) XSurfaceShared(0x10) (XModelSurfs.shared = -2) + the packed geometry blob
    if (sharedDataSize)
    {
        fx::XSurfaceShared sh{};
        std::memset(&sh, 0, sizeof(sh));
        stamp(&sh, off::SH_data, PTR_FOLLOWS); // data follows
        sh.dataSize = sharedDataSize;
        sh.flags = 0; // 0 = loaded (not streamed/xpak)
        zw.align(7);
        zw.write(&sh, sizeof(sh)); // 0x10 bytes
        zw.align(15);
        zw.write(cv.sharedBlob.data(), cv.sharedBlob.size()); // the packed geometry blob
    }

    zw.popStream(); // VIRTUAL -> TEMP_PRELOAD
    zw.popStream(); // TEMP_PRELOAD -> outer

    zt::info(
        "iw8 xsurface(dump) '%s': wrote XModelSurfs (%u surf(s), %u verts, %u tris, shared blob "
        "%u bytes; dropped %u rigid-run(s), %u blend-word(s))",
        name.c_str(), numsurfs, cv.totalVerts, cv.totalTris, sharedDataSize, cv.droppedRigidRuns,
        cv.droppedBlendVerts);
}

bool writeXModelSurfsFromDump(ZoneWriter &zw, const std::string &dumpDir, const std::string &name)
{
    // Parse + convert EAGERLY so we can decide full-vs-minimal and report success, but REGISTER the
    // emit deferred via zw.add(): the body must run during ZoneWriter::build() (after the
    // XAssetList root + the XAsset[] array are framed), exactly like
    // writeXModel/writeMaterial/addImageAsset. Emitting bytes eagerly here would corrupt the zone
    // (body bytes ahead of the XAssetList root, and no XAsset entry framed). Capture the converted
    // result by value so the body owns its data.
    dumpsrc::XseFile xse = dumpsrc::loadXseFile(dumpDir, name);
    if (!xse.loaded)
    {
        zw.add(ASSET_TYPE_XMODELSURFS, name, [name](ZoneWriter &w) { emitMinimalBody(w, name); });
        return false;
    }

    conv_xsurf::Iw8Surfs cv = conv_xsurf::convert(xse);
    if (!cv.ok || cv.surfaces.empty())
    {
        zw.add(ASSET_TYPE_XMODELSURFS, name, [name](ZoneWriter &w) { emitMinimalBody(w, name); });
        return false;
    }

    zw.add(ASSET_TYPE_XMODELSURFS, name, [name, cv](ZoneWriter &w) { emitFullBody(w, name, cv); });
    return true;
}

} // namespace iw8xs_dump
