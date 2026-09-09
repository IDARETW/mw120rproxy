#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <algorithm>
namespace collisionfile {
inline constexpr uint32_t Solid = 0x1, MissileClip = 0x80, VehicleClip = 0x200, ItemClip = 0x400,
                          AiNoSight = 0x1000, ShotClip = 0x2000, PlayerClip = 0x10000,
                          AiClip = 0x20000;
inline constexpr uint32_t SupportedContents =
    Solid | MissileClip | VehicleClip | ItemClip | AiNoSight | ShotClip | PlayerClip | AiClip;
struct Brush {
    float mins[3]{}, maxs[3]{};
    std::vector<std::array<float, 3>> vertices;
    uint32_t contents = Solid;
};
inline bool Parse(const std::vector<uint8_t>& data, std::vector<Brush>& result) {
    result.clear();
    if (data.size() < 12)
        return false;
    const bool typed = memcmp(data.data(), "MWCOLL03", 8) == 0;
    const bool convex = typed || memcmp(data.data(), "MWCOLL02", 8) == 0;
    if (!convex && memcmp(data.data(), "MWCOLL01", 8))
        return false;
    uint32_t count = 0;
    memcpy(&count, data.data() + 8, 4);
    if (!count || count > 32768 || (!convex && data.size() != 12 + size_t(count) * 24))
        return false;
    std::vector<Brush> parsed(count);
    size_t cursor = 12;
    for (auto& b : parsed) {
        if (convex) {
            if (cursor + 4 > data.size())
                return false;
            uint32_t n;
            memcpy(&n, data.data() + cursor, 4);
            cursor += 4;
            if (typed) {
                if (cursor + 4 > data.size())
                    return false;
                memcpy(&b.contents, data.data() + cursor, 4);
                cursor += 4;
                if (!b.contents || (b.contents & ~SupportedContents))
                    return false;
            }
            if (n < 4 || n > 252 || cursor + size_t(n) * 12 > data.size())
                return false;
            b.vertices.resize(n);
            memcpy(b.vertices.data(), data.data() + cursor, n * 12);
            cursor += n * 12;
            for (unsigned k = 0; k < 3; ++k) {
                b.mins[k] = 100001;
                b.maxs[k] = -100001;
            }
            for (const auto& v : b.vertices)
                for (unsigned k = 0; k < 3; ++k) {
                    if (!std::isfinite(v[k]) || std::abs(v[k]) > 100000)
                        return false;
                    b.mins[k] = std::min(b.mins[k], v[k]);
                    b.maxs[k] = std::max(b.maxs[k], v[k]);
                }
            // Reject coplanar input before passing it to the native hull builder.
            bool volume = false;
            const auto& a = b.vertices[0];
            for (size_t i = 1; i < n && !volume; ++i)
                for (size_t j = i + 1; j < n && !volume; ++j) {
                    const auto& p = b.vertices[i];
                    const auto& q = b.vertices[j];
                    const double u[]{p[0] - a[0], p[1] - a[1], p[2] - a[2]},
                        v[]{q[0] - a[0], q[1] - a[1], q[2] - a[2]};
                    const double c[]{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                     u[0] * v[1] - u[1] * v[0]};
                    for (size_t k = j + 1; k < n && !volume; ++k) {
                        const auto& r = b.vertices[k];
                        volume = std::abs(c[0] * (r[0] - a[0]) + c[1] * (r[1] - a[1]) +
                                          c[2] * (r[2] - a[2])) > 0.00001;
                    }
                }
            if (!volume)
                return false;
        } else {
            memcpy(b.mins, data.data() + cursor, 12);
            memcpy(b.maxs, data.data() + cursor + 12, 12);
            cursor += 24;
        }
    }
    if (cursor != data.size())
        return false;
    for (const auto& b : parsed)
        for (unsigned k = 0; k < 3; ++k)
            if (!std::isfinite(b.mins[k]) || !std::isfinite(b.maxs[k]) || b.mins[k] >= b.maxs[k] ||
                b.mins[k] < -100000 || b.maxs[k] > 100000)
                return false;
    result = std::move(parsed);
    return true;
}
inline bool Load(const std::filesystem::path& path, std::vector<Brush>& result) {
    result.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const auto size = file.tellg();
    if (size < 12 || size > 12 + 32768 * (8 + 252 * 12))
        return false;
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    return bool(file.read(reinterpret_cast<char*>(data.data()), size)) && Parse(data, result);
}
}
