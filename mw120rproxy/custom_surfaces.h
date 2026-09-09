#pragma once
#include "custom_physics.h"
#include "inline_hook.h"
#include "logger.h"
#include "replay_trace.h"
#include <algorithm>
#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace customsurfaces {
hook::Status Install(uintptr_t base);
void ObserveMovement(const void* pm);
struct Triangle {
    float p[3][3];
    unsigned type;
};
static_assert(sizeof(Triangle) == 40);
struct Data {
    std::vector<Triangle> triangles;
    std::unordered_map<uint64_t, std::vector<unsigned>> cells;
};
inline std::atomic<std::shared_ptr<const Data>> data;
inline std::atomic<unsigned> samples{0};
inline std::atomic<unsigned> movementEpoch{0};
inline uint64_t Key(int x, int y) {
    return uint64_t(uint32_t(x)) << 32 | uint32_t(y);
}
inline void Clear() {
    data.store(nullptr);
    ++movementEpoch;
}
inline void Load(const std::filesystem::path& directory) {
    Clear();
    auto result = std::make_shared<Data>();
    std::ifstream f(directory / "footsteps.bin", std::ios::binary | std::ios::ate);
    if (f) {
        const auto bytes = f.tellg();
        char magic[8]{};
        unsigned count = 0;
        f.seekg(0);
        if (!f.read(magic, 8) || memcmp(magic, "MWRSTEP1", 8) ||
            !f.read(reinterpret_cast<char*>(&count), 4) || count > 600000 ||
            bytes != 12 + uint64_t(count) * 40) {
            LOG_WARN("Footsteps", "invalid sidecar; using concrete fallback");
            return;
        }
        result->triangles.resize(count);
        if (!f.read(reinterpret_cast<char*>(result->triangles.data()), size_t(count) * 40))
            return;
        size_t entries = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto& t = result->triangles[i];
            if (t.type < 1 || t.type > 28)
                return;
            for (const auto& p : t.p)
                for (float v : p)
                    if (!std::isfinite(v) || std::abs(v) > 100000)
                        return;
            int lo[2], hi[2];
            for (unsigned k = 0; k < 2; ++k) {
                lo[k] = int(std::floor((std::min)({t.p[0][k], t.p[1][k], t.p[2][k]}) / 128));
                hi[k] = int(std::floor((std::max)({t.p[0][k], t.p[1][k], t.p[2][k]}) / 128));
            }
            entries += size_t(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1);
            if (entries > 4000000)
                return;
            for (int x = lo[0]; x <= hi[0]; ++x)
                for (int y = lo[1]; y <= hi[1]; ++y)
                    result->cells[Key(x, y)].push_back(i);
        }
    }
    LOG_INFO("Footsteps", "loaded %zu walkable surface triangles; concrete fallback enabled",
             result->triangles.size());
    samples = 0;
    data.store(std::move(result));
}
inline unsigned TypeAt(const Data& d, const float* p) {
    auto it = d.cells.find(Key(int(std::floor(p[0] / 128)), int(std::floor(p[1] / 128))));
    if (it == d.cells.end())
        return 5;
    float best = 12.f;
    unsigned type = 5;
    for (unsigned i : it->second) {
        const auto& t = d.triangles[i];
        const auto* a = t.p[0];
        const auto* b = t.p[1];
        const auto* c = t.p[2];
        float ux = b[0] - a[0], uy = b[1] - a[1], vx = c[0] - a[0], vy = c[1] - a[1],
              det = ux * vy - uy * vx;
        if (std::abs(det) < .001f)
            continue;
        float x = p[0] - a[0], y = p[1] - a[1], u = (x * vy - y * vx) / det,
              v = (ux * y - uy * x) / det;
        if (u < -.001f || v < -.001f || u + v > 1.001f)
            continue;
        float z = a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]), distance = std::abs(p[2] - z);
        if (distance < best) {
            best = distance;
            type = t.type;
        }
    }
    return type;
}
inline void Apply(void* trace, const float* start, const float* end) {
    if (!customphysics::OwnsEmptyWorld())
        return;
    auto* b = static_cast<unsigned char*>(trace);
    float fraction, normalZ;
    unsigned flags, hitType;
    unsigned short entity;
    memcpy(&fraction, b, 4);
    memcpy(&normalZ, b + replaytrace::NormalZ, 4);
    memcpy(&flags, b + 0x1C, 4);
    memcpy(&hitType, b + 0x24, 4);
    memcpy(&entity, b + 0x2C, 2);
    // Exact Replay PhysicsScript_GetSurfaceTypeFromFlags: (flags >> 19) & 63.
    // Imported bodies currently share PM_Concrete; replace that generic type
    // with authored face data. Preserve other native types and entity hits.
    const unsigned nativeType = (flags >> 19) & 63;
    if (!std::isfinite(fraction) || fraction < 0 || fraction >= 1 ||
        !std::isfinite(normalZ) || normalZ < .35f ||
        hitType != 1 || entity != 2046 || (nativeType && nativeType != 5))
        return;
    float p[3];
    for (unsigned k = 0; k < 3; ++k)
        p[k] = start[k] + fraction * (end[k] - start[k]);
    auto current = data.load();
    unsigned type = current ? TypeAt(*current, p) : 5;
    flags = (flags & ~0x1F80000u) | (type << 19);
    memcpy(b + 0x1C, &flags, 4);
    if (samples.fetch_add(1) < 4)
        LOG_INFO("Footsteps", "world ground surface=%u at (%.1f %.1f %.1f)", type, p[0], p[1],
                 p[2]);
}
inline void ApplyShot(void* trace, const float* start, const float* end) {
    if (!customphysics::OwnsEmptyWorld())
        return;
    auto* b = static_cast<unsigned char*>(trace);
    float fraction;
    unsigned flags, hitType;
    unsigned short entity;
    memcpy(&fraction, b, 4);
    memcpy(&flags, b + 0x1C, 4);
    memcpy(&hitType, b + 0x24, 4);
    memcpy(&entity, b + 0x2C, 2);
    if (!std::isfinite(fraction) || fraction < 0 || fraction >= 1 || hitType != 1 ||
        entity != 2046 || ((flags >> 19) & 63))
        return;
    // Shape tags from generated hulls do not carry a surface type. Give every
    // world hit a valid impact/penetration material, then recover authored floors.
    flags |= 5u << 19;
    memcpy(b + 0x1C, &flags, 4);
    Apply(trace, start, end);
}
}
