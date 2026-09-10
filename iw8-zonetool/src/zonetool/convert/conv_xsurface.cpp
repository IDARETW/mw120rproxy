#include "conv_xsurface.h"
#include "../dumpsrc/xse_dump.h"
#include "xsurface_convert.h"
#include <cstring>

namespace conv_xsurf
{

// Compute a surface's AABB (mid/half) from its IW5 model-space positions. half clamped to >0 so the
// position packer never divides by zero (a degenerate axis collapses to the midpoint).
static void computeBounds(const std::vector<dumpsrc::XseVertex> &v, float mid[3], float half[3])
{
    if (v.empty())
    {
        mid[0] = mid[1] = mid[2] = 0;
        half[0] = half[1] = half[2] = 1;
        return;
    }
    float mn[3] = {v[0].xyz[0], v[0].xyz[1], v[0].xyz[2]};
    float mx[3] = {v[0].xyz[0], v[0].xyz[1], v[0].xyz[2]};
    for (const auto &gv : v)
        for (int k = 0; k < 3; ++k)
        {
            if (gv.xyz[k] < mn[k])
                mn[k] = gv.xyz[k];
            if (gv.xyz[k] > mx[k])
                mx[k] = gv.xyz[k];
        }
    for (int k = 0; k < 3; ++k)
    {
        mid[k] = (mn[k] + mx[k]) * 0.5f;
        half[k] = (mx[k] - mn[k]) * 0.5f;
        if (half[k] < 1e-4f)
            half[k] = 1e-4f; // avoid /0 in the packer; tiny extent is harmless
    }
}

Iw8Surfs convert(const dumpsrc::XseFile &xse)
{
    Iw8Surfs out;
    if (!xse.loaded || xse.surfaces.empty())
        return out; // ok=false
    out.name = xse.name;

    // XModelSurfs.partBits[32] = the ModelSurface-level partBits (IW5 6 ints) widened into the
    // 8-u32 (32-byte) IW8 field. IW5 carries 6 ints; we copy the first 8 slots' worth (the trailing
    // 2 are 0).
    for (int k = 0; k < 6; ++k)
        out.partBits[k] = (uint32_t)xse.modelSurfPartBits[k];

    out.surfaces.resize(xse.surfaces.size());

    auto align16 = [&]() {
        while (out.sharedBlob.size() & 0xF)
            out.sharedBlob.push_back(0);
    };
    auto append = [&](const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        out.sharedBlob.insert(out.sharedBlob.end(), b, b + n);
    };

    for (size_t i = 0; i < xse.surfaces.size(); ++i)
    {
        const dumpsrc::XseSurface &s = xse.surfaces[i];
        Iw8SurfaceCvt &d = out.surfaces[i];

        d.vertCount = (uint16_t)s.verticies.size();
        d.triCount = (uint16_t)s.triIndices.size() / 3; // index count -> tri count
        for (int k = 0; k < 6; ++k)
            d.partBits[k] = (uint32_t)s.partBits[k];

        // per-surface bounds (also the position-pack center/extent).
        float mid[3], half[3];
        computeBounds(s.verticies, mid, half);
        for (int k = 0; k < 3; ++k)
        {
            d.boundsMid[k] = mid[k];
            d.boundsHalf[k] = half[k];
        }

        // ---- verts region (GfxPackedVertex[vertCount]) ----
        align16();
        d.sharedVertDataOffset = (uint32_t)out.sharedBlob.size();
        for (const auto &sv : s.verticies)
        {
            // decode IW5 packed fields to floats
            float uv[2];
            xsurf_conv::unpackTexCoords(sv.texCoord, uv);
            float nrm[3];
            xsurf_conv::unpackUnitVec(sv.normal, nrm);
            float tan[3];
            xsurf_conv::unpackUnitVec(sv.tangent, tan);

            // repack to IW8 GfxPackedVertex (0x14, pack(1) — byte-faithful 20-byte stride)
            uint8_t pv[20];
            uint64_t xyz = xsurf_conv::packPosition(sv.xyz, mid, half);
            uint32_t selfVis = 0;
            uint32_t tc = xsurf_conv::packTexCoords(uv);
            uint32_t tf = xsurf_conv::packTangentFrame(nrm, tan, sv.binormalSign);
            std::memcpy(pv + 0x00, &xyz, 8);
            std::memcpy(pv + 0x08, &selfVis, 4);
            std::memcpy(pv + 0x0C, &tc, 4);
            std::memcpy(pv + 0x10, &tf, 4);
            append(pv, sizeof(pv));
        }

        // ---- indices region (u16[triCount*3]) ----
        align16();
        d.sharedIndexDataOffset = (uint32_t)out.sharedBlob.size();
        if (!s.triIndices.empty())
            append(s.triIndices.data(), s.triIndices.size() * sizeof(uint16_t));

        out.totalVerts += d.vertCount;
        out.totalTris += d.triCount;
        out.droppedRigidRuns += (uint32_t)s.rigidVertLists.size();
        out.droppedBlendVerts += (uint32_t)s.vertsBlend.size();
    }
    align16();

    out.ok = true;
    return out;
}

} // namespace conv_xsurf
