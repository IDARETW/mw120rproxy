#pragma once
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace glassfile {
using Vec = std::array<float, 3>;
inline Vec Sub(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline float Dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Vec Cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
struct Pane {
    Vec normal;
    std::vector<Vec> vertices;
    std::vector<unsigned> surfaces;
    float halfThickness = .125f;
};
inline const char* ShatterEffect(const Pane& pane) {
    float spanSquared = 0;
    for (const auto& a : pane.vertices)
        for (const auto& b : pane.vertices) {
            const auto delta = Sub(a, b);
            spanSquared = (std::max)(spanSquared, Dot(delta, delta));
        }
    return spanSquared > 48 * 48 ? "vfx/code/glass/glass_shatter_64x64"
                                 : "vfx/code/glass/glass_shatter_32x32";
}
inline bool
Load(const std::filesystem::path& path, std::vector<Pane>& out, unsigned& surfaceCount) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() < 16 || f.tellg() > 4 * 1024 * 1024)
        return false;
    f.seekg(0);
    char magic[8];
    unsigned count = 0;
    f.read(magic, 8);
    f.read(reinterpret_cast<char*>(&count), 4);
    f.read(reinterpret_cast<char*>(&surfaceCount), 4);
    const bool v2 = memcmp(magic, "MWRGLS02", 8) == 0;
    if ((!v2 && memcmp(magic, "MWRGLS01", 8)) || count > 1024 || !surfaceCount ||
        surfaceCount > 4096)
        return false;
    std::vector<Pane> data;
    std::vector<bool> used(surfaceCount);
    for (unsigned i = 0; i < count; ++i) {
        Pane p;
        unsigned nv = 0, ns = 0;
        f.read(reinterpret_cast<char*>(&nv), 4);
        f.read(reinterpret_cast<char*>(&ns), 4);
        if (!f || nv < 3 || nv > 32 || !ns || ns > surfaceCount)
            return false;
        f.read(reinterpret_cast<char*>(p.normal.data()), 12);
        if (v2)
            f.read(reinterpret_cast<char*>(&p.halfThickness), 4);
        if (!f || !std::isfinite(p.halfThickness) || p.halfThickness < .125f || p.halfThickness > 8)
            return false;
        p.vertices.resize(nv);
        p.surfaces.resize(ns);
        f.read(reinterpret_cast<char*>(p.vertices.data()), nv * 12);
        f.read(reinterpret_cast<char*>(p.surfaces.data()), ns * 4);
        if (!f || !std::isfinite(Dot(p.normal, p.normal)) ||
            std::abs(Dot(p.normal, p.normal) - 1) > .01f)
            return false;
        for (const auto& v : p.vertices) {
            for (float x : v)
                if (!std::isfinite(x) || std::abs(x) > 100000)
                    return false;
            if (std::abs(Dot(Sub(v, p.vertices[0]), p.normal)) > .5f)
                return false;
        }
        for (unsigned s : p.surfaces) {
            if (s >= surfaceCount || used[s])
                return false;
            used[s] = true;
        }
        // Serialized polygons must have nondegenerate edges and convex CCW winding.
        float area = 0;
        for (unsigned k = 0; k < nv; ++k) {
            auto edge = Sub(p.vertices[(k + 1) % nv], p.vertices[k]);
            auto inward = Cross(p.normal, edge);
            if (Dot(edge, edge) < 1e-6f)
                return false;
            for (const auto& v : p.vertices)
                if (Dot(Sub(v, p.vertices[k]), inward) < -.05f)
                    return false;
            area += Dot(Cross(Sub(p.vertices[k], p.vertices[0]),
                              Sub(p.vertices[(k + 1) % nv], p.vertices[0])),
                        p.normal);
        }
        if (area < 1)
            return false;
        data.push_back(std::move(p));
    }
    if (f.peek() != std::char_traits<char>::eof())
        return false;
    out = std::move(data);
    return true;
}
// Swept bounds against a finite convex pane. Point bounds implement bullet rays.
inline bool Hit(const Pane& p,
                const float* start,
                const float* end,
                const float* bounds,
                float limit,
                float& fraction,
                Vec& normal,
                bool overlap = false) {
    Vec a, b;
    for (unsigned k = 0; k < 3; ++k) {
        a[k] = start[k] + bounds[k];
        b[k] = end[k] + bounds[k];
    }
    const auto delta = Sub(b, a);
    const float d0 = Dot(Sub(a, p.vertices[0]), p.normal),
                d1 = Dot(Sub(b, p.vertices[0]), p.normal);
    float extent = p.halfThickness;
    for (unsigned k = 0; k < 3; ++k)
        extent += std::abs(p.normal[k]) * bounds[k + 3];
    const float side = d0 >= 0 ? 1.f : -1.f, velocity = side * (d1 - d0);
    if (overlap && std::abs(d0) <= extent)
        fraction = 0;
    else {
        if (velocity >= -1e-5f)
            return false;
        fraction = (extent - side * d0) / velocity;
        if (fraction < -.01f || fraction > limit)
            return false;
        fraction = (std::max)(0.f, fraction);
    }
    Vec hit;
    for (unsigned k = 0; k < 3; ++k) {
        hit[k] = a[k] + delta[k] * fraction;
        normal[k] = side * p.normal[k];
    }
    for (unsigned i = 0; i < p.vertices.size(); ++i) {
        const auto edge = Sub(p.vertices[(i + 1) % p.vertices.size()], p.vertices[i]);
        const auto inward = Cross(p.normal, edge);
        float support = 0;
        for (unsigned k = 0; k < 3; ++k)
            support += std::abs(inward[k]) * bounds[k + 3];
        if (Dot(Sub(hit, p.vertices[i]), inward) < -support - .01f)
            return false;
    }
    return true;
}
}
