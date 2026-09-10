#pragma once
#include <cstdint>

namespace xsurf_conv
{

// ---- IW3 packed-field decoders ------------------------------------------------------------------
// IW3 PackedUnitVec: 4 signed bytes -> unit vector. byte/127.0 - bias; w byte unused for direction.
// (CoD4 R_Unpack: x = b0/127.5 - 1, etc. We use the common (b - 127)/127 form which IW3/IW4 use.)
void unpackUnitVec(uint32_t packed, float out[3]);

// IW3 PackedTexCoords: two 16-bit half-floats (u,v). Decode to float pair.
void unpackTexCoords(uint32_t packed, float out[2]);

// half-float (IEEE 754 binary16) -> float.
float halfToFloat(uint16_t h);
// float -> half-float (binary16). Round-to-nearest-even, clamps to representable range.
uint16_t floatToHalf(float f);

// ---- IW8 packers (engine-neutral float -> IW8 packed forms)
// -------------------------------------- IW8 PackedQuatDec3n tangent-frame: a quaternion (xyz dec3n
// + w sign) encoding the normal+tangent basis, packed into one u32. For the load-safe converter we
// pack the NORMAL as a unit-vec dec3n (the engine derives a usable tangent frame); full
// quaternion-basis fidelity is a render-quality refinement, not a load gate. Returns the packed
// u32.
uint32_t packTangentFrame(const float normal[3], const float tangent[3], float binormalSign);

// IW8 PackedTexCoords: two 16-bit half-floats packed into one u32 (lo=u, hi=v).
uint32_t packTexCoords(const float uv[2]);

// IW8 PackedPosition (xyz -> u64): quantize model-space xyz into the surface bounds. For the
// converter we pass the bounds explicitly; if half==0 the component is centered. Returns packed
// u64.
uint64_t packPosition(const float pos[3], const float mid[3], const float half[3]);

} // namespace xsurf_conv
