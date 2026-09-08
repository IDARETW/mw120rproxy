

#pragma once
#include <cstdint>

namespace xsurf_conv {

void unpackUnitVec(uint32_t packed, float out[3]);

// IW3 PackedTexCoords: two 16-bit half-floats (u,v). Decode to float pair.
void unpackTexCoords(uint32_t packed, float out[2]);

// half-float (IEEE 754 binary16) -> float.
float halfToFloat(uint16_t h);
// float -> half-float (binary16). Round-to-nearest-even, clamps to representable range.
uint16_t floatToHalf(float f);

uint32_t packTangentFrame(const float normal[3], const float tangent[3], float binormalSign);

// IW8 PackedTexCoords: two 16-bit half-floats packed into one u32 (lo=u, hi=v).
uint32_t packTexCoords(const float uv[2]);

uint64_t packPosition(const float pos[3], const float mid[3], const float half[3]);

} // namespace xsurf_conv
