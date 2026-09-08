// xsurface.cpp — IW3<->IW8 geometry conversion helpers (xsurface family).
// Implements xsurface_convert.h: IW3 packed-field decode + IW8 packed-field encode. Pure math, no IO.
#include "xsurface_convert.h"
#include <cstring>
#include <cmath>

namespace xsurf_conv {

// ---- half-float <-> float -----------------------------------------------------------------------
float halfToFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;                                   // +/- zero
        } else {
            // subnormal half -> normalized float
            exp = 1;
            while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            f = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (mant << 13);          // inf / nan
    } else {
        f = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out;
    std::memcpy(&out, &f, 4);
    return out;
}

uint16_t floatToHalf(float fv) {
    uint32_t f;
    std::memcpy(&f, &fv, 4);
    const uint32_t sign = (f >> 16) & 0x8000u;
    int32_t  exp  = (int32_t)((f >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = f & 0x7FFFFFu;
    if (((f >> 23) & 0xFFu) == 0xFF) {                  // inf / nan
        return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0));
    }
    if (exp >= 0x1F) return (uint16_t)(sign | 0x7C00u); // overflow -> inf
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;           // underflow -> zero
        mant |= 0x800000u;                              // restore implicit 1
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half_mant = mant >> shift;
        // round to nearest even
        if ((mant >> (shift - 1)) & 1u) ++half_mant;
        return (uint16_t)(sign | half_mant);
    }
    uint16_t half_mant = (uint16_t)(mant >> 13);
    if (mant & 0x1000u) {                               // round to nearest even
        uint16_t r = (uint16_t)((sign | (exp << 10) | half_mant) + 1);
        return r;
    }
    return (uint16_t)(sign | (exp << 10) | half_mant);
}

// ---- IW3 packed-field decoders ------------------------------------------------------------------
void unpackUnitVec(uint32_t packed, float out[3]) {
    // IW3/IW4 PackedUnitVec: 4 bytes b0..b3; direction = (b - 127) * (1/127) (b3 = builtScale, unused).
    const uint8_t b0 = (uint8_t)(packed & 0xFF);
    const uint8_t b1 = (uint8_t)((packed >> 8) & 0xFF);
    const uint8_t b2 = (uint8_t)((packed >> 16) & 0xFF);
    float x = ((float)b0 - 127.0f) / 127.0f;
    float y = ((float)b1 - 127.0f) / 127.0f;
    float z = ((float)b2 - 127.0f) / 127.0f;
    // normalize (the packed form is already near-unit, but renormalize for safety)
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1e-8f) { x /= len; y /= len; z /= len; }
    out[0] = x; out[1] = y; out[2] = z;
}

void unpackTexCoords(uint32_t packed, float out[2]) {
    out[0] = halfToFloat((uint16_t)(packed & 0xFFFF));
    out[1] = halfToFloat((uint16_t)((packed >> 16) & 0xFFFF));
}

// ---- IW8 packers --------------------------------------------------------------------------------
// dec3n: pack a unit vec into 10:10:10 signed-normalized (the low 30 bits of a u32; top 2 bits = w
// sign/quadrant). This is the storage form IW8's PackedQuatDec3n uses for the basis vector. For the
// load-safe prototype we encode the normal as a dec3n unit vec; the engine reconstructs a tangent
// frame from it (binormalSign drives handedness via the top bit).
static uint32_t packDec3n(const float v[3], float wSign) {
    auto enc = [](float c) -> uint32_t {
        if (c >  1.0f) c =  1.0f;
        if (c < -1.0f) c = -1.0f;
        int32_t q = (int32_t)std::lround(c * 511.0f);   // [-511,511] in 10-bit signed
        return (uint32_t)(q & 0x3FF);
    };
    uint32_t x = enc(v[0]);
    uint32_t y = enc(v[1]);
    uint32_t z = enc(v[2]);
    uint32_t w = (wSign < 0.0f) ? 0x3u : 0x1u;          // 2-bit w (nonzero => valid)
    return (w << 30) | (z << 20) | (y << 10) | x;
}

uint32_t packTangentFrame(const float normal[3], const float tangent[3], float binormalSign) {
    (void)tangent; // prototype: encode the normal basis; tangent derived engine-side
    return packDec3n(normal, binormalSign);
}

uint32_t packTexCoords(const float uv[2]) {
    uint32_t u = floatToHalf(uv[0]);
    uint32_t v = floatToHalf(uv[1]);
    return (v << 16) | u;
}

uint64_t packPosition(const float pos[3], const float mid[3], const float half[3]) {
    // PackedPosition (IW8): xyz quantized into the surface bounds as 21-bit-per-axis signed-norm.
    // q = clamp((p - mid)/half, -1, 1) * (2^20 - 1) ; packed lo->hi x(21),y(21),z(21), top bit pad.
    auto enc = [](float p, float m, float h) -> uint64_t {
        float c = (h > 1e-8f) ? (p - m) / h : 0.0f;
        if (c >  1.0f) c =  1.0f;
        if (c < -1.0f) c = -1.0f;
        int64_t q = (int64_t)std::llround(c * 1048575.0); // 2^20 - 1
        return (uint64_t)(q & 0x1FFFFF);                  // 21 bits
    };
    uint64_t x = enc(pos[0], mid[0], half[0]);
    uint64_t y = enc(pos[1], mid[1], half[1]);
    uint64_t z = enc(pos[2], mid[2], half[2]);
    return (z << 42) | (y << 21) | x;
}

} // namespace xsurf_conv
