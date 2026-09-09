#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

namespace ambientgrid {
using Vec = std::array<float, 3>;
struct Cell {
    std::array<int32_t, 3> coordinate;
    uint16_t palette;
    uint8_t primaryLight, traceMask;
};
static_assert(sizeof(Cell) == 16);
struct Grid {
    std::vector<Cell> cells;
    std::vector<Vec> colors;
    const Cell* Find(const std::array<int32_t, 3>& coordinate) const {
        const auto it = std::lower_bound(cells.begin(), cells.end(), coordinate,
                                         [](const Cell& cell, const auto& key) {
                                             return cell.coordinate < key;
                                         });
        return it != cells.end() && it->coordinate == coordinate ? &*it : nullptr;
    }
};
inline uint16_t Half(float value) {
    if (!(value > 0))
        return 0;
    if (value < std::ldexp(1.f, -14))
        return uint16_t(std::nearbyint(std::ldexp(value, 24)));
    int exponent = 0;
    const float fraction = std::frexp(value, &exponent);
    const unsigned mantissa = unsigned(std::nearbyint(fraction * 2048)) - 1024;
    return uint16_t(((exponent + 14) << 10) + mantissa);
}
inline std::array<uint8_t, 64> Probe(const Vec& irradiance) {
    std::array<uint8_t, 64> probe{};
    for (unsigned k = 0; k < 3; ++k) {
        const uint16_t dc = Half(irradiance[k] / .886226925f);
        memcpy(probe.data() + k * 18, &dc, 2);
    }
    const uint16_t one = 0x3C00;
    memcpy(probe.data() + 54, &one, 2);
    memcpy(probe.data() + 56, &one, 2);
    return probe;
}
inline bool ValidProbe(const std::array<uint8_t, 64>& probe) {
    for (unsigned i = 0; i < 29; ++i) {
        uint16_t value = 0;
        memcpy(&value, probe.data() + i * 2, 2);
        if ((value & 0x7C00) == 0x7C00)
            return false;
    }
    return true;
}
inline bool Parse(const std::vector<uint8_t>& bytes, Grid& grid) {
    grid = {};
    if (bytes.size() < 16 || memcmp(bytes.data(), "MWLGRID1", 8))
        return false;
    uint32_t count = 0, palette = 0;
    memcpy(&count, bytes.data() + 8, 4);
    memcpy(&palette, bytes.data() + 12, 4);
    if (!count || count > 2000000 || !palette || palette > 65536 ||
        bytes.size() != 16 + uint64_t(count) * 16 + uint64_t(palette) * 12)
        return false;
    Grid parsed;
    parsed.cells.resize(count);
    parsed.colors.resize(palette);
    memcpy(parsed.cells.data(), bytes.data() + 16, size_t(count) * 16);
    memcpy(parsed.colors.data(), bytes.data() + 16 + size_t(count) * 16, size_t(palette) * 12);
    std::array<int32_t, 3> previous{};
    for (size_t i = 0; i < count; ++i) {
        const auto& cell = parsed.cells[i];
        if (cell.palette >= palette || (i && !(previous < cell.coordinate)))
            return false;
        for (unsigned k = 0; k < 3; ++k)
            if (std::abs(int64_t(cell.coordinate[k])) * (k == 2 ? 64 : 32) > 100000)
                return false;
        previous = cell.coordinate;
    }
    for (const auto& color : parsed.colors)
        for (float value : color)
            if (!std::isfinite(value) || value < 0 || value > 8)
                return false;
    grid = std::move(parsed);
    return true;
}
inline bool Load(const std::filesystem::path& file, Grid& grid) {
    grid = {};
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream)
        return false;
    const auto size = stream.tellg();
    if (size < 16 || size > 16 + 2000000 * 16 + 65536 * 12)
        return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    return stream.read(reinterpret_cast<char*>(bytes.data()), size) && Parse(bytes, grid);
}
inline Vec Position(const Cell& cell) {
    return {cell.coordinate[0] * 32.f, cell.coordinate[1] * 32.f, cell.coordinate[2] * 64.f};
}
template <class Visible>
bool Sample(const Grid& grid, const Vec& position, Visible visible, Vec& result) {
    std::array<int32_t, 3> base{};
    Vec fraction{};
    for (unsigned k = 0; k < 3; ++k) {
        if (!std::isfinite(position[k]) || std::abs(position[k]) > 100000)
            return false;
        const float value = position[k] / (k == 2 ? 64.f : 32.f);
        base[k] = int32_t(std::floor(value));
        fraction[k] = value - base[k];
    }
    Vec sum{};
    float total = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        auto coordinate = base;
        float weight = 1;
        for (unsigned k = 0; k < 3; ++k) {
            const bool upper = (corner & (1u << k)) != 0;
            coordinate[k] += upper;
            weight *= upper ? fraction[k] : 1 - fraction[k];
        }
        if (weight <= 0)
            continue;
        const auto* cell = grid.Find(coordinate);
        if (!cell || !visible(position, Position(*cell)))
            continue;
        for (unsigned k = 0; k < 3; ++k)
            sum[k] += weight * grid.colors[cell->palette][k];
        total += weight;
    }
    if (total > .001f) {
        for (unsigned k = 0; k < 3; ++k)
            result[k] = sum[k] / total;
        return true;
    }
    // Sparse grids can omit a complete interpolation cell near walls. Search
    // only the immediate neighborhood; never borrow a remote floor's lighting.
    std::vector<std::pair<float, const Cell*>> nearby;
    nearby.reserve(125);
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            for (int z = -1; z <= 1; ++z) {
                const auto* cell = grid.Find({base[0] + x, base[1] + y, base[2] + z});
                if (!cell)
                    continue;
                const auto p = Position(*cell);
                float distance = 0;
                for (unsigned k = 0; k < 3; ++k)
                    distance += (p[k] - position[k]) * (p[k] - position[k]);
                if (distance <= 96 * 96)
                    nearby.emplace_back(distance, cell);
            }
    std::sort(nearby.begin(), nearby.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& [distance, cell] : nearby)
        if (visible(position, Position(*cell))) {
            result = grid.colors[cell->palette];
            return true;
        }
    return false;
}
}
