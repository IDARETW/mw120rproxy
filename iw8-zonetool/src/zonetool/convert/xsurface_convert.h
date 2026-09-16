#pragma once
#include <cstdint>

namespace xsurf_conv
{

// ---- IW3 packed-field decoders ------------------------------------------------------------------
// IW3 PackedUnitVec: 4 signed bytes -> unit vector. byte/127.0 - bias; w byte unused for direction.
// (CoD4 R_Unpack: x = b0/127.5 - 1, etc. We use the common (b - 127)/127 form which IW3/IW4 use.)
void unpackUnitVec(uint32_t packed, float out[3]);

// IW5 .xse PackedTexCoords: high halfword U, low halfword V.
void unpackTexCoords(uint32_t packed, float out[2]);

// half-float (IEEE 754 binary16) -> float.
float halfToFloat(uint16_t h);
// float -> half-float (binary16). Round-to-nearest-even; overflow becomes infinity.
uint16_t floatToHalf(float f);

// ---- IW8 packers (engine-neutral float -> IW8 packed forms)
// IW8 QuatDec3n stores three quaternion components (10/10/9 bits), the handedness bit,
// and the index of the omitted largest component. Invalid or degenerate bases are rejected.
uint32_t packTangentFrame(const float normal[3], const float tangent[3], float binormalSign);

// IW8 PackedTexCoords: two 16-bit half-floats packed into one u32 (lo=u, hi=v).
uint32_t packTexCoords(const float uv[2]);

// IW8 PackedPosition uses unsigned 21-bit coordinates and the largest bounds half-extent
// as the common scale. Degenerate bounds decode to their midpoint.
uint64_t packPosition(const float pos[3], const float mid[3], const float half[3]);

} // namespace xsurf_conv
