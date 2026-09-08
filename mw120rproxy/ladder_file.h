#pragma once
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace ladderfile {
struct Face {
    float bottom[3], top[3], normal[3], width;
    float grip[3], rungDistance = 12, gripWidth = 15.2496f;
};
static_assert(sizeof(Face) == 60);
inline bool Load(const std::filesystem::path& path, std::vector<Face>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return false;
    const auto size = f.tellg();
    if (size < 12 || size > 12 + 512 * 60)
        return false;
    f.seekg(0);
    char magic[8]{};
    unsigned count = 0;
    f.read(magic, 8);
    f.read(reinterpret_cast<char*>(&count), 4);
    const bool v2 = memcmp(magic, "MWRLAD02", 8) == 0;
    const unsigned stride = v2 ? 60 : 40;
    if ((!v2 && memcmp(magic, "MWRLAD01", 8)) || count > 512 || size != 12 + stride * count)
        return false;
    std::vector<Face> data(count);
    for (auto& v : data) {
        f.read(reinterpret_cast<char*>(&v), stride);
        if (!v2) {
            memcpy(v.grip, v.bottom, 12);
            v.gripWidth = (std::min)(v.width, 15.2496f);
        }
    }
    if (!f)
        return false;
    for (const auto& v : data) {
        const auto* p = reinterpret_cast<const float*>(&v);
        for (unsigned k = 0; k < 15; ++k)
            if (!std::isfinite(p[k]) || std::abs(p[k]) > 100000)
                return false;
        const float height = v.top[2] - v.bottom[2];
        if (std::abs(v.bottom[0] - v.top[0]) > .01f || std::abs(v.bottom[1] - v.top[1]) > .01f ||
            height < 48 || height > 8192 || std::abs(v.normal[2]) > .001f ||
            std::abs(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] - 1) > .001f ||
            v.width < 12 || v.width > 1024 || v.rungDistance < 4 || v.rungDistance > 64 ||
            v.gripWidth < 8 || v.gripWidth > 128 || std::abs(v.grip[2] - v.bottom[2]) > 64)
            return false;
    }
    out = std::move(data);
    return true;
}
}
