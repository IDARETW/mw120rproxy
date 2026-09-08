

#include "../convert/registry.h"
#include "../convert/xsurface_binfmt.h"
#include "../convert/xsurface_convert.h"
#include "../common/log.h"
#include "iw3_zone.h"
#include "iw3_structs.h"
#include "xsurface_read.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <vector>

namespace iw3xs {

using iw3::LoadCtx;

// Copy `n` bytes from absolute zone offset `off` into `dst`. Bounds-checked against the flat buffer.
static bool readAt(LoadCtx& lc, size_t off, void* dst, size_t n) {
    if (off == LoadCtx::npos)
        return false;
    if (off + n > lc.size()) {
        zt::err("iw3 xsurface: read past end (off=%zu n=%zu size=%zu)", off, n, lc.size());
        return false;
    }
    std::memcpy(dst, lc.base() + off, n);
    return true;
}
template <typename T> static bool readAt(LoadCtx& lc, size_t off, T& v) {
    return readAt(lc, off, &v, sizeof(T));
}

// Resolve an IW3 serialized pointer value to an absolute offset. `cursorForFollows` is the position a
// "follows" (-1) field points at (the byte right after the struct that owns the field, in load order).
static size_t resolvePtr(LoadCtx& lc, uint32_t v, size_t cursorForFollows) {
    if (v == 0u)
        return LoadCtx::npos;
    if (v == 0xFFFFFFFFu)
        return cursorForFollows; // follows inline
    bool follows = false;
    return lc.resolve(v, &follows); // packed offset alias
}

bool dumpXSurfacesForModel(LoadCtx& lc,
                           iw3sr::ZoneSource& zs,
                           uint32_t surfsOff,
                           uint32_t numsurfs,
                           const std::string& name) {
    if (numsurfs == 0 || surfsOff == 0 || surfsOff == (uint32_t)LoadCtx::npos) {
        zt::warn("iw3 xsurface '%s': no surfaces (numsurfs=%u) — skipping", name.c_str(), numsurfs);
        return true;
    }

    // Build the .xsurf_bin payload in memory, then wrap+write once.
    std::vector<uint8_t> payload;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        payload.insert(payload.end(), b, b + n);
    };

    // --- 1) read all IW3 XSurface structs (the array is contiguous at surfsOff) ---
    std::vector<iw3::XSurface> surfs(numsurfs);
    for (uint32_t i = 0; i < numsurfs; ++i) {
        if (!readAt(lc, surfsOff + (size_t)i * sizeof(iw3::XSurface), surfs[i]))
            return false;
    }

    // --- 2) file header + name ---
    xsurf_bin::FileHeader fh{};
    fh.surfCount = numsurfs;
    fh.nameLen = (uint32_t)(name.size() + 1);
    std::memset(fh.partBits, 0, sizeof(fh.partBits));
    // widen first surface's partBits[4] into the model-level header (IW3 keeps per-LOD partBits; the
    // XModelSurfs-level partBits in IW8 is the union — first surf's bits are a safe, common choice).
    for (int k = 0; k < 4; ++k)
        fh.partBits[k] = (uint32_t)surfs[0].partBits[k];
    fh.flags = 0;
    put(&fh, sizeof(fh));
    put(name.c_str(), name.size() + 1);

    // --- 3) per-surface headers (contiguous), then per-surface arrays ---
    std::vector<xsurf_bin::SurfHeader> shdrs(numsurfs);

    const size_t afterSurfArray = surfsOff + (size_t)numsurfs * sizeof(iw3::XSurface);

    struct SurfPayload {
        std::vector<xsurf_bin::GfxVertex> verts;
        std::vector<uint16_t> indices;
        std::vector<xsurf_bin::RigidVertList> rigid;
        std::vector<uint16_t> blendVerts;
    };
    std::vector<SurfPayload> bodies(numsurfs);

    for (uint32_t i = 0; i < numsurfs; ++i) {
        const iw3::XSurface& s = surfs[i];
        xsurf_bin::SurfHeader& h = shdrs[i];
        std::memset(&h, 0, sizeof(h));
        h.flags = 0;
        h.tileMode = (uint8_t)s.tileMode;
        h.deformed = (uint8_t)(s.deformed ? 1 : 0);
        h.vertCount = s.vertCount;
        h.triCount = s.triCount;
        h.baseTriIndex = s.baseTriIndex;
        h.baseVertIndex = s.baseVertIndex;
        h.rigidVertListCount = (uint8_t)(s.vertListCount & 0xFF);
        for (int k = 0; k < 4; ++k)
            h.vertBlendCounts[k] = (uint16_t)s.vertInfo.vertCount[k];
        for (int k = 0; k < 4; ++k)
            h.partBits[k] = (uint32_t)s.partBits[k];

        SurfPayload& bp = bodies[i];

        // verts0 -> GfxPackedVertex[vertCount] (IW3 fat 0x20 verts) -> unpack to neutral GfxVertex.
        size_t vertsOff = resolvePtr(lc, s.verts0.off, afterSurfArray);
        if (s.vertCount && vertsOff != LoadCtx::npos) {
            bp.verts.resize(s.vertCount);
            for (uint16_t v = 0; v < s.vertCount; ++v) {
                iw3::GfxPackedVertex pv{};
                if (!readAt(lc, vertsOff + (size_t)v * sizeof(iw3::GfxPackedVertex), pv))
                    return false;
                xsurf_bin::GfxVertex& gv = bp.verts[v];
                gv.pos[0] = pv.xyz[0];
                gv.pos[1] = pv.xyz[1];
                gv.pos[2] = pv.xyz[2];
                gv.binormalSign = pv.binormalSign;
                xsurf_conv::unpackUnitVec(pv.normal.packed, gv.normal);
                xsurf_conv::unpackUnitVec(pv.tangent.packed, gv.tangent);
                xsurf_conv::unpackTexCoords(pv.texCoord.packed, gv.uv);
                gv.color[0] = (uint8_t)(pv.color.packed & 0xFF);
                gv.color[1] = (uint8_t)((pv.color.packed >> 8) & 0xFF);
                gv.color[2] = (uint8_t)((pv.color.packed >> 16) & 0xFF);
                gv.color[3] = (uint8_t)((pv.color.packed >> 24) & 0xFF);
            }
        }

        // triIndices -> uint16_t[triCount*3].
        size_t triOff = resolvePtr(lc, s.triIndices.off, afterSurfArray);
        if (s.triCount && triOff != LoadCtx::npos) {
            const size_t nIdx = (size_t)s.triCount * 3;
            bp.indices.resize(nIdx);
            if (!readAt(lc, triOff, bp.indices.data(), nIdx * sizeof(uint16_t)))
                return false;
        }

        size_t vlOff = resolvePtr(lc, s.vertList.off, afterSurfArray);
        if (s.vertListCount && vlOff != LoadCtx::npos) {
            bp.rigid.resize(s.vertListCount);
            for (uint32_t r = 0; r < s.vertListCount; ++r) {
                iw3::XRigidVertList rv{};
                if (!readAt(lc, vlOff + (size_t)r * sizeof(iw3::XRigidVertList), rv))
                    return false;
                bp.rigid[r].boneOffset = rv.boneOffset;
                bp.rigid[r].vertCount = rv.vertCount;
                bp.rigid[r].triOffset = rv.triOffset;
                bp.rigid[r].triCount = rv.triCount;
            }
        }

        // vertsBlend (IW3 XSurfaceVertexInfo.vertsBlend): blend-weight index stream. Word count per
        // IW3 = vertCount[1]*3 + vertCount[2]*5 + vertCount[3]*7 (tier 0 verts carry no blend words).
        const uint32_t blendWords = (uint32_t)s.vertInfo.vertCount[1] * 3 +
                                    (uint32_t)s.vertInfo.vertCount[2] * 5 +
                                    (uint32_t)s.vertInfo.vertCount[3] * 7;
        size_t vbOff = resolvePtr(lc, s.vertInfo.vertsBlend.off, afterSurfArray);
        if (blendWords && vbOff != LoadCtx::npos) {
            bp.blendVerts.resize(blendWords);
            if (!readAt(lc, vbOff, bp.blendVerts.data(), blendWords * sizeof(uint16_t)))
                return false;
        }
        h.blendVertWords = (uint32_t)bp.blendVerts.size();
    }

    // --- 4) serialize: all headers, then all bodies (matches the .xsurf_bin field order) ---
    for (uint32_t i = 0; i < numsurfs; ++i)
        put(&shdrs[i], sizeof(xsurf_bin::SurfHeader));
    for (uint32_t i = 0; i < numsurfs; ++i) {
        SurfPayload& bp = bodies[i];
        if (!bp.verts.empty())
            put(bp.verts.data(), bp.verts.size() * sizeof(xsurf_bin::GfxVertex));
        if (!bp.indices.empty())
            put(bp.indices.data(), bp.indices.size() * sizeof(uint16_t));
        if (!bp.rigid.empty())
            put(bp.rigid.data(), bp.rigid.size() * sizeof(xsurf_bin::RigidVertList));
        if (!bp.blendVerts.empty())
            put(bp.blendVerts.data(), bp.blendVerts.size() * sizeof(uint16_t));
    }

    auto wrapped = iw3sr::ZoneSource::wrapBlob(xsurf_bin::MAGIC, xsurf_bin::VERSION, payload.data(),
                                               payload.size());
    if (!zs.addXSurface(name, wrapped)) {
        zt::err("iw3 xsurface '%s': failed to write .xsurf_bin", name.c_str());
        return false;
    }
    zt::info("iw3 xsurface '%s': dumped %u surf(s), %zu payload bytes", name.c_str(), numsurfs,
             payload.size());
    return true;
}

} // namespace iw3xs

namespace convert {

// Registry hook: standalone dispatch (cursor at an XSurface-array head). IW3 rarely dispatches
// XModelSurfs standalone (the surfaces ride inside XModel), so without a parent count/name this is a
// safe no-op that logs; the real geometry is dumped via the xmodel reader -> dumpXSurfacesForModel.
void iw3_dump_xsurface(iw3::LoadCtx& lc, iw3sr::ZoneSource& zs) {
    (void)lc;
    (void)zs;
    zt::info("iw3_dump xsurface: standalone dispatch (IW3 surfaces ride inside XModel; "
             "dumped via the xmodel reader). nothing to do here.");
}

} // namespace convert
