// asset_xsurface.cpp — Stage-B IW8 xsurface writer (replaces the foundation stub).
// =================================================================================================
// Rebuilds an IW8 (MW2019 1.24) XModelSurfs(8)=0x60 graph from xsurface/<name>.xsurf_bin and emits it
// into the ZoneWriter in LOAD-READ ORDER with the raw IW8 sentinels (-2 follows / 0 null). All struct
// field offsets/sizes come from src/iw8/iw8_focus_structs.h (dev.i64-pinned, static_assert'd vs retail
// g_assetSizes: XModelSurfs=0x60, XSurface=0xC0, XSurfaceShared=0x10, XPakEntryInfo=0x20).
//
// GEOMETRY MODEL (IW8): the bulk vert/index data lives in XSurfaceShared.data (one packed blob) and
// each XSurface references regions of it via its shared*Offset fields. We:
//   * build ONE shared blob = [ GfxPackedVertex verts ][ u16 indices ] for all surfaces concatenated,
//   * stamp each XSurface's sharedVertDataOffset / sharedIndexDataOffset to its region,
//   * attach the blob via XModelSurfs.shared (and mirror the descriptor pointer on each XSurface).
//
// LOAD-SAFETY (SPEC §1: full render geometry is NOT a prototype goal; the bar is "loads"): every
// pointer field is either a valid follows(-2) with inline data of the exact counted size, or null(0).
// No packed-offset alias arithmetic is used, so there is nothing for the loader to mis-resolve.
//
// Pointer-graph WRITE ORDER (mirrors the IW-line loader reading //PTR fields in struct order):
//   XModelSurfs{...} -> name -> surfs[ XSurface{...} x N ] -> per-surf trailing ptr data(none here)
//                    -> XSurfaceShared{...} -> shared.data blob.
// We follow that exact order so DB_ReadXFile's sequential front-to-back read lands each datum where
// the field's -2 marker said it would.
// =================================================================================================
#include "../convert/registry.h"
#include "../convert/xsurface_binfmt.h"
#include "../convert/xsurface_convert.h"
#include "../common/log.h"
#include "iw8_zone.h"
#include "iw8_structs.h"
#include "iw8_focus_structs.h"
#include "xsurface_write.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <vector>

namespace iw8xs {

using namespace iw8;
namespace fx = iw8_focus;

// Parsed view of a .xsurf_bin (engine-neutral). Owns its arrays.
struct ParsedSurf {
    xsurf_bin::SurfHeader              hdr{};
    std::vector<xsurf_bin::GfxVertex>  verts;
    std::vector<uint16_t>              indices;     // triCount*3
    std::vector<xsurf_bin::RigidVertList> rigid;
    std::vector<uint16_t>              blendVerts;
};
struct ParsedFile {
    std::string                name;
    uint32_t                   partBits[8]{};
    std::vector<ParsedSurf>    surfs;
};

// Parse the unwrapped .xsurf_bin payload. Returns false on malformed input.
static bool parseBin(const std::vector<uint8_t>& payload, ParsedFile& out) {
    size_t p = 0;
    auto need = [&](size_t n) -> bool { return p + n <= payload.size(); };
    auto take = [&](void* dst, size_t n) -> bool {
        if (!need(n)) return false;
        std::memcpy(dst, payload.data() + p, n); p += n; return true;
    };

    xsurf_bin::FileHeader fh{};
    if (!take(&fh, sizeof(fh))) return false;
    std::memcpy(out.partBits, fh.partBits, sizeof(out.partBits));

    if (!need(fh.nameLen)) return false;
    out.name.assign(reinterpret_cast<const char*>(payload.data() + p),
                    fh.nameLen ? fh.nameLen - 1 : 0);   // exclude trailing NUL
    p += fh.nameLen;

    out.surfs.resize(fh.surfCount);
    // headers first (contiguous), then bodies (per the binfmt contract)
    for (uint32_t i = 0; i < fh.surfCount; ++i) {
        if (!take(&out.surfs[i].hdr, sizeof(xsurf_bin::SurfHeader))) return false;
    }
    for (uint32_t i = 0; i < fh.surfCount; ++i) {
        ParsedSurf& s = out.surfs[i];
        const xsurf_bin::SurfHeader& h = s.hdr;
        if (h.vertCount) {
            s.verts.resize(h.vertCount);
            if (!take(s.verts.data(), (size_t)h.vertCount * sizeof(xsurf_bin::GfxVertex))) return false;
        }
        if (h.triCount) {
            s.indices.resize((size_t)h.triCount * 3);
            if (!take(s.indices.data(), s.indices.size() * sizeof(uint16_t))) return false;
        }
        if (h.rigidVertListCount) {
            s.rigid.resize(h.rigidVertListCount);
            if (!take(s.rigid.data(), (size_t)h.rigidVertListCount * sizeof(xsurf_bin::RigidVertList))) return false;
        }
        if (h.blendVertWords) {
            s.blendVerts.resize(h.blendVertWords);
            if (!take(s.blendVerts.data(), (size_t)h.blendVertWords * sizeof(uint16_t))) return false;
        }
    }
    return true;
}

// Compute a surface's bounds (mid/half) from its unpacked positions (for PackedPosition quantization).
static void computeBounds(const std::vector<xsurf_bin::GfxVertex>& v, float mid[3], float half[3]) {
    if (v.empty()) { mid[0]=mid[1]=mid[2]=0; half[0]=half[1]=half[2]=1; return; }
    float mn[3] = { v[0].pos[0], v[0].pos[1], v[0].pos[2] };
    float mx[3] = { v[0].pos[0], v[0].pos[1], v[0].pos[2] };
    for (const auto& gv : v) for (int k = 0; k < 3; ++k) {
        if (gv.pos[k] < mn[k]) mn[k] = gv.pos[k];
        if (gv.pos[k] > mx[k]) mx[k] = gv.pos[k];
    }
    for (int k = 0; k < 3; ++k) { mid[k] = (mn[k]+mx[k])*0.5f; half[k] = (mx[k]-mn[k])*0.5f; }
}

// Emit a minimal, load-safe name-only XModelSurfs (used when the blob is missing/corrupt).
static void writeMinimal(ZoneWriter& zw, const std::string& name) {
    fx::XModelSurfs xs{}; std::memset(&xs, 0, sizeof(xs));
    // name = -2 (follows), surfs = 0, shared = 0, numsurfs = 0.
    uint64_t followsN = PTR_FOLLOWS, nullN = PTR_NULL;
    std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x00, &followsN, 8); // name
    std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x08, &nullN,    8); // surfs
    std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x30, &nullN,    8); // shared
    xs.numsurfs = 0;

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);
    zw.write(&xs, sizeof(xs));
        zw.pushStream(XFILE_BLOCK_VIRTUAL);
        zw.writeStr(name);
        zw.popStream();
    zw.popStream();
    zt::warn("iw8 xsurface '%s': emitted minimal name-only XModelSurfs (no geometry)", name.c_str());
}

bool writeXModelSurfs(ZoneWriter& zw, iw3sr::ZoneSource& zs, const std::string& name) {
    std::vector<uint8_t> blob;
    if (!zs.getXSurface(name, blob)) {
        zt::warn("iw8 xsurface '%s': no .xsurf_bin in zone-source", name.c_str());
        writeMinimal(zw, name);
        return false;
    }
    std::string magic; uint32_t ver = 0; std::vector<uint8_t> payload;
    if (!iw3sr::ZoneSource::unwrapBlob(blob, magic, ver, payload) ||
        magic != xsurf_bin::MAGIC) {
        zt::err("iw8 xsurface '%s': bad blob magic='%s' ver=%u", name.c_str(), magic.c_str(), ver);
        writeMinimal(zw, name);
        return false;
    }
    ParsedFile pf;
    if (!parseBin(payload, pf)) {
        zt::err("iw8 xsurface '%s': corrupt .xsurf_bin payload", name.c_str());
        writeMinimal(zw, name);
        return false;
    }
    const uint16_t numsurfs = (uint16_t)pf.surfs.size();
    if (numsurfs == 0) { writeMinimal(zw, name); return false; }

    // ---- build the shared geometry blob: per surf [ GfxPackedVertex verts ][ u16 indices ] -------
    // Record each surf's byte offsets into the blob so we can stamp the XSurface shared*Offset fields.
    std::vector<uint32_t> vertOff(numsurfs), idxOff(numsurfs);
    std::vector<uint8_t>  shared;                  // the packed XSurfaceShared.data
    auto appendBlob = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        shared.insert(shared.end(), b, b + n);
    };
    auto align16 = [&]() { while (shared.size() & 0xF) shared.push_back(0); };

    for (uint16_t i = 0; i < numsurfs; ++i) {
        const ParsedSurf& s = pf.surfs[i];
        float mid[3], half[3]; computeBounds(s.verts, mid, half);

        align16();
        vertOff[i] = (uint32_t)shared.size();
        for (const auto& gv : s.verts) {
            fx::GfxPackedVertex pv{};
            pv.xyz           = xsurf_conv::packPosition(gv.pos, mid, half);
            pv.selfVisibility = 0;
            pv.texCoord      = xsurf_conv::packTexCoords(gv.uv);
            pv.tangentFrame  = xsurf_conv::packTangentFrame(gv.normal, gv.tangent, gv.binormalSign);
            appendBlob(&pv, sizeof(pv));           // 0x14 stride (pack(1))
        }

        align16();
        idxOff[i] = (uint32_t)shared.size();
        if (!s.indices.empty()) appendBlob(s.indices.data(), s.indices.size() * sizeof(uint16_t));
    }
    align16();
    const uint32_t sharedDataSize = (uint32_t)shared.size();

    // =================================================================================================
    //  EMIT — load-read order.
    //  XModelSurfs(0x60) -> TEMP_PRELOAD(1); name + surfs[] + XSurfaceShared + shared.data -> VIRTUAL(8)
    // =================================================================================================
    fx::XModelSurfs xs{}; std::memset(&xs, 0, sizeof(xs));
    {
        uint64_t followsN = PTR_FOLLOWS, nullN = PTR_NULL;
        std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x00, &followsN, 8);  // name   (follows)
        std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x08, &followsN, 8);  // surfs  (follows)
        // xpakEntry (0x10..0x2F) stays zero (not streamed; geometry is inline).
        std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x30,
                    (sharedDataSize ? &followsN : &nullN), 8);              // shared (follows|null)
    }
    xs.numsurfs = numsurfs;
    xs.ugbState = 0;
    // partBits[32] @0x3C: copy the 8-int model-level partBits (32 bytes).
    std::memcpy(reinterpret_cast<uint8_t*>(&xs) + 0x3C, pf.partBits, 32);

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);
    zw.write(&xs, sizeof(xs));                                  // 0x60 bytes

    zw.pushStream(XFILE_BLOCK_VIRTUAL);
        // 1) name (XModelSurfs.name = -2)
        zw.writeStr(name);

        // 2) surfs[numsurfs] (XModelSurfs.surfs = -2): the XSurface(0xC0) array.
        zw.align(7);
        for (uint16_t i = 0; i < numsurfs; ++i) {
            const ParsedSurf& s = pf.surfs[i];
            fx::XSurface su{}; std::memset(&su, 0, sizeof(su));
            su.flags          = 0;
            su.vertCount      = s.hdr.vertCount;
            su.triCount       = s.hdr.triCount;
            su.blendShapeTargetCount = 0;
            su.rigidVertListCount    = 0;          // rigid runs dropped (load-safe; not render-required)
            su.subdivLevelCount      = 0;
            su.ugbID          = 0;
            su.hash           = 0;
            su.blendVertSize  = 0;
            su.sharedVertDataOffset  = vertOff[i];
            su.sharedIndexDataOffset = idxOff[i];
            su.sharedTriClusterDataOffset      = 0;
            su.sharedColorDataOffset           = 0;
            su.sharedSecondUVDataOffset        = 0;
            su.sharedNormalTransformDataOffset = 0;
            su.sharedTensionAccumTableOffset   = 0;
            su.sharedTensionDataOffset         = 0;
            // pointer fields (0x48 shared .. 0xB8): all null for load-safety — the geometry is reachable
            // via XModelSurfs.shared; per-surface trailing ptr arrays are not emitted, so they MUST be 0.
            // (su is zero-initialized, so shared/lmapCoords/rigidVertLists/blendVerts/subdiv/childBounds/
            //  blendShapesPerVert/blendShapesRecalcTangentFrameData are already 0 = null.)
            // surfBounds (0x90, 0x18): set from this surf's positions.
            float mid[3], half[3]; computeBounds(s.verts, mid, half);
            su.surfBounds.midPoint = { { mid[0], mid[1], mid[2] } };
            su.surfBounds.halfSize = { { half[0], half[1], half[2] } };
            // partBits[32] @0x70.
            std::memcpy(su.partBits, s.hdr.partBits, 32);
            zw.write(&su, sizeof(su));             // 0xC0 bytes
        }

        // 3) XSurfaceShared (XModelSurfs.shared = -2) + its data blob.
        if (sharedDataSize) {
            fx::XSurfaceShared sh{}; std::memset(&sh, 0, sizeof(sh));
            uint64_t followsN = PTR_FOLLOWS;
            std::memcpy(reinterpret_cast<uint8_t*>(&sh) + 0x00, &followsN, 8); // data (follows)
            sh.dataSize = sharedDataSize;
            sh.flags    = 0;                       // 0 = loaded (not streamed/xpak)
            zw.align(7);
            zw.write(&sh, sizeof(sh));             // 0x10 bytes
            zw.align(15);
            zw.write(shared.data(), shared.size());// the packed geometry blob
        }
    zw.popStream();   // VIRTUAL -> TEMP_PRELOAD
    zw.popStream();   // TEMP_PRELOAD -> outer

    zt::info("iw8 xsurface '%s': wrote XModelSurfs (%u surf(s), shared blob %u bytes)",
             name.c_str(), numsurfs, sharedDataSize);
    return true;
}

} // namespace iw8xs

namespace convert {

// Registry hook: forward to the family writer.
void iw8_write_xsurface(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name) {
    iw8xs::writeXModelSurfs(zw, zs, name ? std::string(name) : std::string());
}

} // namespace convert
