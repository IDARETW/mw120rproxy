#include "conv_xsurface.h"
#include "../dumpsrc/xse_dump.h"
#include "xsurface_convert.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

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

static std::array<float, 3> unpackPosition(uint64_t packed, const float mid[3], const float half[3])
{
    constexpr double reciprocal = 1.0 / 2097151.0;
    const float scale = std::max({half[0], half[1], half[2]});
    std::array<float, 3> position{};
    for (size_t axis = 0; axis < 3; ++axis)
    {
        const auto value = static_cast<uint32_t>((packed >> (axis * 21)) & 0x1FFFFFu);
        position[axis] = static_cast<float>((value * reciprocal * 2.0 - 1.0) * scale + mid[axis]);
    }
    return position;
}

static void appendTriClusters(std::vector<uint8_t> &blob,
                              const std::vector<std::array<float, 3>> &positions,
                              const std::vector<uint16_t> &indices)
{
    constexpr size_t trianglesPerCluster = 64;
    constexpr uint32_t noConeDirection = 0xE0080200u;
    constexpr uint32_t noConeApex = 0;
    constexpr uint32_t noConeCutoff = 0xFF7FFFFFu;
    const size_t triangleCount = indices.size() / 3;

    for (size_t firstTriangle = 0; firstTriangle < triangleCount;
         firstTriangle += trianglesPerCluster)
    {
        std::array<float, 3> minimum{
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
        };
        std::array<float, 3> maximum{
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
        };
        const size_t endTriangle = std::min(firstTriangle + trianglesPerCluster, triangleCount);
        for (size_t triangle = firstTriangle; triangle < endTriangle; ++triangle)
            for (size_t corner = 0; corner < 3; ++corner)
            {
                const auto &position = positions[indices[triangle * 3 + corner]];
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], position[axis]);
                    maximum[axis] = std::max(maximum[axis], position[axis]);
                }
            }

        uint16_t packedBounds[6]{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            packedBounds[axis] = xsurf_conv::floatToHalf((minimum[axis] + maximum[axis]) * 0.5f);
            packedBounds[axis + 3] =
                xsurf_conv::floatToHalf((maximum[axis] - minimum[axis]) * 0.5f);
            if ((packedBounds[axis] & 0x7C00u) == 0x7C00u ||
                (packedBounds[axis + 3] & 0x7C00u) == 0x7C00u)
                throw std::runtime_error(
                    "Model triangle-cluster bounds exceed Replay's half-float range");
        }
        if (blob.size() > UINT32_MAX - 24u)
            throw std::runtime_error("Model triangle-cluster stream exceeds Replay's offset range");
        const auto begin = reinterpret_cast<const uint8_t *>(packedBounds);
        blob.insert(blob.end(), begin, begin + sizeof(packedBounds));
        for (const uint32_t value : {noConeDirection, noConeApex, noConeCutoff})
        {
            const auto bytes = reinterpret_cast<const uint8_t *>(&value);
            blob.insert(blob.end(), bytes, bytes + sizeof(value));
        }
    }
}

Iw8Surfs convert(const dumpsrc::XseFile &xse)
{
    Iw8Surfs out;
    if (!xse.loaded || xse.surfaces.empty())
        return out; // ok=false
    if (xse.surfaces.size() > UINT16_MAX)
        throw std::runtime_error("Too many model surfaces for Replay: " + xse.name);
    out.name = xse.name;

    // XModelSurfs.partBits[32] = the ModelSurface-level partBits (IW5 6 ints) widened into the
    // 8-u32 (32-byte) IW8 field. IW5 carries 6 ints; we copy the first 8 slots' worth (the trailing
    // 2 are 0).
    for (int k = 0; k < 6; ++k)
        out.partBits[k] = (uint32_t)xse.modelSurfPartBits[k];

    out.surfaces.resize(xse.surfaces.size());

    auto align16 = [&]() {
        if (out.sharedBlob.size() > UINT32_MAX - 15u)
            throw std::runtime_error("Model shared buffer exceeds Replay's offset range: " +
                                     xse.name);
        while (out.sharedBlob.size() & 0xF)
            out.sharedBlob.push_back(0);
    };
    auto append = [&](const void *p, size_t n) {
        if (n > UINT32_MAX - out.sharedBlob.size())
            throw std::runtime_error("Model shared buffer exceeds Replay's offset range: " +
                                     xse.name);
        const uint8_t *b = static_cast<const uint8_t *>(p);
        out.sharedBlob.insert(out.sharedBlob.end(), b, b + n);
    };

    for (size_t i = 0; i < xse.surfaces.size(); ++i)
    {
        const dumpsrc::XseSurface &s = xse.surfaces[i];
        Iw8SurfaceCvt &d = out.surfaces[i];

        if (s.verticies.empty() || s.verticies.size() > UINT16_MAX || s.triIndices.empty() ||
            s.triIndices.size() % 3 || s.triIndices.size() / 3 > UINT16_MAX)
            throw std::runtime_error("Invalid Replay model surface counts: " + xse.name);
        for (const auto index : s.triIndices)
            if (index >= s.verticies.size())
                throw std::runtime_error("Model triangle references a missing vertex: " + xse.name);
        d.vertCount = static_cast<uint16_t>(s.verticies.size());
        d.triCount = static_cast<uint16_t>(s.triIndices.size() / 3);
        if (s.rigidVertLists.size() > UINT8_MAX)
            throw std::runtime_error("Too many rigid model groups for Replay: " + xse.name);
        size_t rigidVertices = 0;
        for (const auto &run : s.rigidVertLists)
        {
            if (run.boneOffset % 64 || !run.vertCount ||
                uint32_t(run.triOffset) + run.triCount > d.triCount)
                throw std::runtime_error("Invalid rigid model group: " + xse.name);
            rigidVertices += run.vertCount;
            if (rigidVertices > d.vertCount)
                throw std::runtime_error("Rigid model groups exceed the vertex array: " + xse.name);
            // IW5 stores a 64-byte matrix offset. Replay consumes the raw bone index
            // at RVA 188AC8C, adding the model's bone base without dividing it.
            d.rigidVertLists.push_back({static_cast<uint16_t>(run.boneOffset / 64), run.vertCount,
                                        run.triOffset, run.triCount});
        }
        if (!s.deformed && !s.rigidVertLists.empty() && rigidVertices != d.vertCount)
            throw std::runtime_error("Rigid model groups leave unassigned vertices: " + xse.name);
        size_t blendVertices = 0;
        size_t sourceBlendWords = 0;
        for (size_t tier = 0; tier < 4; ++tier)
        {
            if (s.vertBlendCounts[tier] < 0)
                throw std::runtime_error("Negative model blend count: " + xse.name);
            const auto count = static_cast<uint16_t>(s.vertBlendCounts[tier]);
            d.blendVertCounts[tier] = count;
            blendVertices += count;
            sourceBlendWords += count * (tier * 2 + 1);
        }
        if (sourceBlendWords != s.vertsBlend.size() ||
            (s.deformed ? blendVertices != d.vertCount : blendVertices != 0))
            throw std::runtime_error("Model blend counts do not match the vertex data: " +
                                     xse.name);
        // Replay's rigid/skinned dispatch tests bit 1 (RVA 188ABC5).
        // Bit 0 enables the color pages; the resident color offset alone does not.
        d.flags = s.deformed ? 3 : 1;
        size_t blendCursor = 0;
        for (size_t tier = 0; tier < 4; ++tier)
            for (uint16_t vertex = 0; vertex < d.blendVertCounts[tier]; ++vertex)
            {
                uint32_t weightSum = 0;
                for (size_t word = 0; word < tier * 2 + 1; ++word)
                {
                    auto value = s.vertsBlend[blendCursor++];
                    if (word == 0 || word % 2 == 1)
                    {
                        if (value % 64)
                            throw std::runtime_error("Misaligned model blend bone offset: " +
                                                     xse.name);
                        value /= 64;
                    }
                    else
                        weightSum += value;
                    d.blendVerts.push_back(value);
                }
                if (weightSum > UINT16_MAX)
                    throw std::runtime_error("Model blend weights exceed one: " + xse.name);
                // Replay consumes 2, 4, 6, or 8 words per vertex (RVA 1958750).
                d.blendVerts.push_back(0);
            }
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

        std::vector<std::array<uint8_t, 20>> packedVertices;
        std::vector<std::array<float, 3>> packedPositions;
        packedVertices.reserve(s.verticies.size());
        packedPositions.reserve(s.verticies.size());
        for (const auto &sv : s.verticies)
        {
            float uv[2];
            xsurf_conv::unpackTexCoords(sv.texCoord, uv);
            float nrm[3];
            xsurf_conv::unpackUnitVec(sv.normal, nrm);
            float tan[3];
            xsurf_conv::unpackUnitVec(sv.tangent, tan);

            std::array<uint8_t, 20> pv{};
            const uint64_t xyz = xsurf_conv::packPosition(sv.xyz, mid, half);
            const uint32_t selfVis = 0;
            const uint32_t tc = xsurf_conv::packTexCoords(uv);
            const uint32_t tf = xsurf_conv::packTangentFrame(nrm, tan, sv.binormalSign);
            std::memcpy(pv.data() + 0x00, &xyz, 8);
            std::memcpy(pv.data() + 0x08, &selfVis, 4);
            std::memcpy(pv.data() + 0x0C, &tc, 4);
            std::memcpy(pv.data() + 0x10, &tf, 4);
            packedVertices.push_back(pv);
            packedPositions.push_back(unpackPosition(xyz, mid, half));
        }

        // Replay's resident XSurface buffer keeps the index stream first, followed by an
        // equally-sized zeroed triangle-work region used by the GPU surface setup path.
        align16();
        d.sharedIndexDataOffset = (uint32_t)out.sharedBlob.size();
        const size_t indexBytes = s.triIndices.size() * sizeof(uint16_t);
        append(s.triIndices.data(), indexBytes);
        if (indexBytes > UINT32_MAX - out.sharedBlob.size())
            throw std::runtime_error("Model triangle-work stream exceeds Replay's offset range: " +
                                     xse.name);
        out.sharedBlob.resize(out.sharedBlob.size() + indexBytes, 0);

        d.sharedVertDataOffset = static_cast<uint32_t>(out.sharedBlob.size());
        for (const auto &vertex : packedVertices)
            append(vertex.data(), vertex.size());

        d.sharedTriClusterDataOffset = static_cast<uint32_t>(out.sharedBlob.size());
        appendTriClusters(out.sharedBlob, packedPositions, s.triIndices);

        d.sharedColorDataOffset = static_cast<uint32_t>(out.sharedBlob.size());
        for (const auto &vertex : s.verticies)
        {
            const auto bgra = vertex.color;
            const uint32_t rgba =
                (bgra & 0xFF00FF00u) | ((bgra & 0xFFu) << 16) | ((bgra >> 16) & 0xFFu);
            append(&rgba, sizeof(rgba));
        }

        out.totalVerts += d.vertCount;
        out.totalTris += d.triCount;
        out.totalTriClusters += (d.triCount + 63u) / 64u;
        out.totalRigidRuns += static_cast<uint32_t>(d.rigidVertLists.size());
        out.totalBlendWords += static_cast<uint32_t>(d.blendVerts.size());
    }
    align16();

    out.ok = true;
    return out;
}

uint32_t surfaceHash(const Iw8Surfs &surfs, const Iw8SurfaceCvt &surface)
{
    uint32_t hash = 2166136261u;
    const auto hashBytes = [&](const void *data, size_t size) {
        const auto *bytes = static_cast<const uint8_t *>(data);
        for (size_t index = 0; index < size; ++index)
            hash = (hash ^ bytes[index]) * 16777619u;
    };
    const auto hashStream = [&](uint32_t offset, size_t bytes) {
        if (offset > surfs.sharedBlob.size() || bytes > surfs.sharedBlob.size() - offset)
            throw std::runtime_error("Model surface hash exceeds shared geometry: " + surfs.name);
        hashBytes(surfs.sharedBlob.data() + offset, bytes);
    };
    // Hash each surface's own payload; unrelated surfaces and padding are not its identity.
    hashStream(surface.sharedIndexDataOffset, size_t(surface.triCount) * 12);
    hashStream(surface.sharedVertDataOffset, size_t(surface.vertCount) * 20);
    hashStream(surface.sharedTriClusterDataOffset, ((size_t(surface.triCount) + 63) / 64) * 24);
    if (surface.sharedColorDataOffset != UINT32_MAX)
        hashStream(surface.sharedColorDataOffset, size_t(surface.vertCount) * 4);
    hashBytes(surface.boundsMid, sizeof(surface.boundsMid));
    hashBytes(surface.boundsHalf, sizeof(surface.boundsHalf));
    hashBytes(&surface.flags, sizeof(surface.flags));
    hashBytes(surface.blendVertCounts, sizeof(surface.blendVertCounts));
    hashBytes(surface.blendVerts.data(), surface.blendVerts.size() * sizeof(uint16_t));
    hashBytes(surface.rigidVertLists.data(),
              surface.rigidVertLists.size() * sizeof(Iw8RigidVertList));
    return hash == UINT32_MAX ? UINT32_MAX - 1 : hash;
}

} // namespace conv_xsurf
