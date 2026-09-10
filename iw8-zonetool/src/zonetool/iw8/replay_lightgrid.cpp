#include "replay_lightgrid.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace replaylightgrid
{
namespace
{
template <class T> T Read(const uint8_t *p, size_t offset)
{
    T value;
    std::memcpy(&value, p + offset, sizeof(value));
    return value;
}
template <class T> void Put(uint8_t *p, size_t offset, T value)
{
    std::memcpy(p + offset, &value, sizeof(value));
}
} // namespace
Grid Load(const std::string &meshPath)
{
    Grid grid;
    constexpr std::string_view suffix = ".render.json";
    if (!meshPath.ends_with(suffix))
        return grid;
    const auto path = meshPath.substr(0, meshPath.size() - suffix.size()) + ".gpulightgrid.bin";
    if (!std::filesystem::exists(path))
        return grid;
    const auto size = std::filesystem::file_size(path);
    if (size < 264 || size > 512 * 1024 * 1024)
        throw std::runtime_error("Invalid native light-grid byte count");
    grid.payload.resize(size);
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char *>(grid.payload.data()), size))
        throw std::runtime_error("Cannot read native light grid");
    const auto *p = grid.payload.data();
    if (std::memcmp(p, "IW8GLG01", 8))
        throw std::runtime_error("Unknown native light-grid format");
    std::memcpy(grid.counts.data(), p + 8, 16);
    const auto [probes, tets, roots, voxels] = grid.counts;
    if (!probes || probes > 2000000 || !tets || tets > 8000000 || !roots || roots > 1000000 ||
        !voxels || voxels > 8000000 ||
        size != 264ull + probes * 44ull + tets * 32ull + roots * 12ull + voxels * 4ull)
        throw std::runtime_error("Inconsistent native light-grid arrays");
    for (size_t i = 24; i < 136; i += 4)
        if (!std::isfinite(Read<float>(p, i)))
            throw std::runtime_error("Nonfinite light-grid bounds");
    const uint64_t nx = Read<uint32_t>(p, 200), ny = Read<uint32_t>(p, 204);
    const uint64_t nz = Read<uint32_t>(p, 208);
    if (!nx || !ny || !nz || nx > 1000000 || ny > 1000000 || nz > 8000000 || nx * ny != roots ||
        nx * ny * nz != voxels || Read<uint32_t>(p, 216) != 5)
        throw std::runtime_error("Invalid native light-grid voxel dimensions");
    size_t position = 264 + size_t(probes) * 32;
    for (size_t i = 0; i < size_t(probes) * 3; ++i)
        if (!std::isfinite(Read<float>(p, position + i * 4)))
            throw std::runtime_error("Nonfinite light-grid probe position");
    position += size_t(probes) * 12;
    for (size_t i = 0; i < size_t(tets) * 4; ++i)
        if (Read<uint32_t>(p, position + i * 4) >= probes)
            throw std::runtime_error("Invalid light-grid tetrahedron probe");
    position += size_t(tets) * 16;
    for (size_t i = 0; i < size_t(tets) * 4; ++i)
    {
        const auto neighbor = Read<uint32_t>(p, position + i * 4);
        if (neighbor != UINT32_MAX && neighbor >= tets)
            throw std::runtime_error("Invalid light-grid neighbor");
    }
    position += size_t(tets) * 16;
    for (size_t i = 0; i < roots; ++i)
    {
        const auto first = Read<uint32_t>(p, position + i * 12);
        if (first != (0x80000000u | uint32_t(i * nz)) ||
            Read<uint32_t>(p, position + i * 12 + 4) != 0 ||
            Read<uint32_t>(p, position + i * 12 + 8) != nz - 1)
            throw std::runtime_error("Invalid light-grid root leaf range");
    }
    position += size_t(roots) * 12;
    for (size_t i = 0; i < voxels; ++i)
    {
        const auto start = Read<uint32_t>(p, position + i * 4);
        if (start != UINT32_MAX && start >= tets)
            throw std::runtime_error("Invalid light-grid voxel tetrahedron");
    }
    return grid;
}
void Emit(iw8::ZoneWriter &w, const Grid &grid)
{
    if (!grid)
        return;
    const auto [probes, tets, roots, voxels] = grid.counts;
    const auto *p = grid.payload.data();
    // Replay D920A0 aligns the embedded grid to 8. D91B40 consumes 640 bytes;
    // all following arrays stay in the containing virtual stream.
    std::array<uint8_t, 640> body{};
    Put(body.data(), 0, probes);
    for (size_t offset : {8u, 112u, 208u, 216u, 232u, 304u, 456u})
        Put(body.data(), offset, iw8::PTR_FOLLOWS);
    Put(body.data(), 200, 1u);
    Put(body.data(), 224, tets);
    Put(body.data(), 448, voxels);
    std::memcpy(body.data() + 528, p + 24, 112);
    w.align(7);
    w.write(body.data(), body.size());
    size_t cursor = 264;
    auto array = [&](size_t mask, size_t size) {
        w.align(mask);
        w.write(p + cursor, size);
        cursor += size;
    };
    array(31, size_t(probes) * 32);
    array(3, size_t(probes) * 12);
    std::array<uint8_t, 92> zone{};
    Put(zone.data(), 0, probes);
    Put(zone.data(), 8, tets);
    Put(zone.data(), 24, voxels);
    std::memcpy(zone.data() + 28, p + 136, 64);
    w.align(63);
    w.write(zone.data(), zone.size());
    // Replay D921D0: 224-byte voxel tree, then header/roots/internal nodes.
    std::array<uint8_t, 224> tree{};
    Put(tree.data(), 0, roots);
    Put(tree.data(), 4, 1u);
    for (size_t offset : {8u, 16u, 24u})
        Put(tree.data(), offset, iw8::PTR_FOLLOWS);
    w.align(7);
    w.write(tree.data(), tree.size());
    w.align(15);
    w.write(p + 200, 64);
    const auto rootStart = cursor + size_t(tets) * 32;
    w.align(3);
    w.write(p + rootStart, size_t(roots) * 12);
    w.align(3);
    const std::array<uint8_t, 16> emptyNode{};
    w.write(emptyNode.data(), emptyNode.size());
    array(63, size_t(tets) * 16);
    array(63, size_t(tets) * 16);
    cursor += size_t(roots) * 12;
    array(63, size_t(voxels) * 4);
}
} // namespace replaylightgrid
