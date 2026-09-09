#pragma once
#include <cstddef>
#include <cstring>

namespace replaytrace {
// Replay PhysicsQuery_ConvertRaycastResultToLegacyTrace (RVA 0x108C3F0).
inline constexpr size_t Size = 0x48;
inline constexpr size_t Position = 0x04;
inline constexpr size_t Normal = 0x10;
inline constexpr size_t NormalZ = Normal + 2 * sizeof(float);

inline void WriteContact(void* trace, float fraction, const float* start, const float* end,
                         const float* normal) {
    auto* bytes = static_cast<unsigned char*>(trace);
    std::memcpy(bytes, &fraction, sizeof(fraction));
    for (unsigned k = 0; k < 3; ++k) {
        const float position = start[k] + fraction * (end[k] - start[k]);
        std::memcpy(bytes + Position + k * sizeof(float), &position, sizeof(position));
    }
    std::memcpy(bytes + Normal, normal, 3 * sizeof(float));
    bytes[0x3E] = normal[2] >= .7f;
}
}
