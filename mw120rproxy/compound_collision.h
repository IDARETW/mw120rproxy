#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "collision_file.h"
#include <map>

namespace compoundcollision {
inline constexpr size_t Threshold = 512, BatchSize = 256;
inline constexpr size_t BodyCount(size_t hulls) {
    return hulls > Threshold ? (hulls + BatchSize - 1) / BatchSize : hulls;
}
struct Batch {
    uint32_t contents = 0;
    std::vector<size_t> brushes;
};
inline std::vector<Batch> Batches(const std::vector<collisionfile::Brush>& brushes) {
    const size_t limit = brushes.size() > Threshold ? BatchSize : 1;
    std::map<uint32_t, std::vector<size_t>> classes;
    for (size_t i = 0; i < brushes.size(); ++i)
        classes[brushes[i].contents].push_back(i);
    std::vector<Batch> result;
    for (const auto& [contents, indices] : classes)
        for (size_t first = 0; first < indices.size(); first += limit) {
            const auto end = std::min(first + limit, indices.size());
            result.push_back({contents, {indices.begin() + first, indices.begin() + end}});
        }
    return result;
}
// Exact Replay hknpShapeInstance layout and identity initialization at
// RVA 161C98B..161C9E8. Rotation column W contains flags, not a float.
struct alignas(16) Instance {
    float transform[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float scale[4]{1, 1, 1, 1};
    void* shape = nullptr;
    uint16_t shapeTag = 0xFFFF, destructionTag = 0xFFFF;
    uint8_t isEmpty = 0, padding[3]{};
    uint32_t nextEmpty = 0;
    uint16_t instanceId = 0xFFFF, reserved = 0;
    void* parent = nullptr;
    Instance() {
        const uint32_t flags = 0x3F000040;
        std::memcpy(&transform[3], &flags, 4);
    }
};
static_assert(sizeof(Instance) == 112 && alignof(Instance) == 16);
static_assert(offsetof(Instance, shape) == 0x50 && offsetof(Instance, shapeTag) == 0x58 &&
              offsetof(Instance, parent) == 0x68);
struct Array {
    Instance* data;
    int size;
    uint32_t capacity;
};
}
