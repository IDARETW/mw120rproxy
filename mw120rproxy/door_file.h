#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace doorfile {
using Vec = std::array<float, 3>;
using Plane = std::array<float, 4>;
using Hull = std::vector<Plane>;
inline float Dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Vec Rotate(const Vec& p, float angle) {
    const float a = angle * 0.0174532925199433f, c = std::cos(a), s = std::sin(a);
    return {c * p[0] - s * p[1], s * p[0] + c * p[1], p[2]};
}
struct Door {
    char name[64]{};
    unsigned group = 0;
    Vec pivot{}, travel{}, mins{}, maxs{};
    float angle = 0, duration = 1;
    std::vector<Hull> hulls;
    std::vector<std::vector<unsigned>> surfaces;
    unsigned Closed() const {
        return angle != 0 ? 24 : 0;
    }
    float Phase(unsigned frame) const {
        return float(frame) / 24 - (angle != 0 ? 1 : 0);
    }
};
inline Vec Point(const Door& d, const Vec& p, unsigned frame) {
    const float phase = d.Phase(frame);
    Vec local{p[0] - d.pivot[0], p[1] - d.pivot[1], p[2] - d.pivot[2]};
    auto out = Rotate(local, d.angle * phase);
    for (unsigned k = 0; k < 3; ++k)
        out[k] += d.pivot[k] + d.travel[k] * phase;
    return out;
}
inline Plane Transform(const Door& d, const Plane& p, unsigned frame) {
    const float phase = d.Phase(frame);
    Vec n{p[0], p[1], p[2]};
    const auto rotated = Rotate(n, d.angle * phase);
    Vec destination = d.pivot;
    for (unsigned k = 0; k < 3; ++k)
        destination[k] += d.travel[k] * phase;
    return {rotated[0], rotated[1], rotated[2], p[3] - Dot(n, d.pivot) + Dot(rotated, destination)};
}
inline bool Hit(const Door& d,
                unsigned frame,
                const float* start,
                const float* end,
                const float* bounds,
                float limit,
                float& fraction,
                Vec& normal,
                bool overlap = false) {
    bool hit = false;
    for (const auto& hull : d.hulls) {
        float enter = -1, leave = 1;
        Vec candidate{};
        bool outside = false, rejected = false, endOutside = false;
        for (const auto& original : hull) {
            const auto p = Transform(d, original, frame);
            float a = -p[3], b = -p[3];
            for (unsigned k = 0; k < 3; ++k) {
                const float extent = std::abs(p[k]) * bounds[k + 3];
                a += p[k] * (start[k] + bounds[k]) - extent;
                b += p[k] * (end[k] + bounds[k]) - extent;
            }
            outside |= a > 0;
            endOutside |= b > 0;
            if (a > 0 && b >= a) {
                rejected = true;
                break;
            }
            if (a <= 0 && b <= 0)
                continue;
            if (a > b) {
                const float t = (a - 0.01f) / (a - b);
                if (t > enter) {
                    enter = t;
                    candidate = {p[0], p[1], p[2]};
                }
            } else {
                leave = (std::min)(leave, a / (a - b));
            }
        }
        if (rejected)
            continue;
        if (!outside) {
            if (!overlap || endOutside)
                continue;
            enter = 0;
            candidate = {0, 0, 1};
        }
        if (enter <= leave && enter >= -0.001f && enter <= limit) {
            fraction = limit = (std::max)(0.f, enter);
            normal = candidate;
            hit = true;
        }
    }
    return hit;
}
inline bool Load(const std::filesystem::path& path, std::vector<Door>& out, unsigned& surfaces) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() < 16 || f.tellg() > 16 * 1024 * 1024)
        return false;
    f.seekg(0);
    auto read = [&](void* p, size_t n) {
        return bool(f.read(static_cast<char*>(p), n));
    };
    char magic[8];
    unsigned count = 0;
    if (!read(magic, 8) || std::memcmp(magic, "MWRDOR01", 8) || !read(&count, 4) ||
        !read(&surfaces, 4) || count > 32 || !surfaces || surfaces > 4096)
        return false;
    std::vector<Door> doors;
    std::vector<bool> used(surfaces);
    for (unsigned i = 0; i < count; ++i) {
        Door d;
        unsigned frames = 0, hulls = 0;
        float values[14];
        if (!read(d.name, 64) || !d.name[0] || d.name[63] || !read(&d.group, 4) ||
            d.group >= count || !read(&frames, 4) || !read(values, sizeof(values)) ||
            !read(&hulls, 4) || !hulls || hulls > 128)
            return false;
        for (float v : values)
            if (!std::isfinite(v) || std::abs(v) > 100000)
                return false;
        std::copy_n(values, 3, d.pivot.begin());
        d.angle = values[3];
        std::copy_n(values + 4, 3, d.travel.begin());
        std::copy_n(values + 7, 3, d.mins.begin());
        std::copy_n(values + 10, 3, d.maxs.begin());
        d.duration = values[13];
        if (frames != (d.angle ? 49u : 25u) || std::abs(d.angle) > 170 || d.duration < 0.1f ||
            d.duration > 10 || (!d.angle && Dot(d.travel, d.travel) < 1))
            return false;
        for (unsigned k = 0; k < 3; ++k)
            if (d.mins[k] >= d.maxs[k])
                return false;
        for (unsigned h = 0; h < hulls; ++h) {
            unsigned size = 0;
            if (!read(&size, 4) || size < 4 || size > 70)
                return false;
            Hull planes(size);
            if (!read(planes.data(), size * sizeof(Plane)))
                return false;
            for (const auto& p : planes) {
                for (float x : p)
                    if (!std::isfinite(x) || std::abs(x) > 200000)
                        return false;
                if (std::abs(p[0] * p[0] + p[1] * p[1] + p[2] * p[2] - 1) > .01f)
                    return false;
            }
            d.hulls.push_back(std::move(planes));
        }
        for (unsigned frame = 0; frame < frames; ++frame) {
            unsigned n = 0;
            if (!read(&n, 4) || !n || n > surfaces)
                return false;
            std::vector<unsigned> ids(n);
            if (!read(ids.data(), n * 4))
                return false;
            for (unsigned s : ids) {
                if (s >= surfaces || used[s])
                    return false;
                used[s] = true;
            }
            d.surfaces.push_back(std::move(ids));
        }
        doors.push_back(std::move(d));
    }
    if (f.peek() != std::char_traits<char>::eof())
        return false;
    out = std::move(doors);
    return true;
}
}
