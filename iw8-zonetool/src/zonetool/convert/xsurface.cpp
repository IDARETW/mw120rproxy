#include "xsurface_convert.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace xsurf_conv
{

// ---- half-float <-> float -----------------------------------------------------------------------
float halfToFloat(uint16_t h)
{
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t f;
    if (exp == 0)
    {
        if (mant == 0)
        {
            f = sign; // +/- zero
        }
        else
        {
            // subnormal half -> normalized float
            exp = 1;
            while ((mant & 0x400u) == 0)
            {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            f = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1F)
    {
        f = sign | 0x7F800000u | (mant << 13); // inf / nan
    }
    else
    {
        f = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out;
    std::memcpy(&out, &f, 4);
    return out;
}

uint16_t floatToHalf(float fv)
{
    uint32_t f;
    std::memcpy(&f, &fv, 4);
    const uint32_t sign = (f >> 16) & 0x8000u;
    int32_t exp = (int32_t)((f >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = f & 0x7FFFFFu;
    if (((f >> 23) & 0xFFu) == 0xFF)
    { // inf / nan
        return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0));
    }
    if (exp >= 0x1F)
        return (uint16_t)(sign | 0x7C00u); // overflow -> inf
    if (exp <= 0)
    {
        if (exp < -10)
            return (uint16_t)sign; // underflow -> zero
        mant |= 0x800000u;         // restore implicit 1
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half_mant = mant >> shift;
        // round to nearest even
        const uint32_t remainder = mant & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (half_mant & 1u)))
            ++half_mant;
        return (uint16_t)(sign | half_mant);
    }
    uint16_t half_mant = (uint16_t)(mant >> 13);
    const uint32_t remainder = mant & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (half_mant & 1u)))
    { // round to nearest even
        uint16_t r = (uint16_t)((sign | (exp << 10) | half_mant) + 1);
        return r;
    }
    return (uint16_t)(sign | (exp << 10) | half_mant);
}

// ---- IW3 packed-field decoders ------------------------------------------------------------------
void unpackUnitVec(uint32_t packed, float out[3])
{
    // IW3/IW4 PackedUnitVec: 4 bytes b0..b3; direction = (b - 127) * (1/127) (b3 = builtScale,
    // unused).
    const uint8_t b0 = (uint8_t)(packed & 0xFF);
    const uint8_t b1 = (uint8_t)((packed >> 8) & 0xFF);
    const uint8_t b2 = (uint8_t)((packed >> 16) & 0xFF);
    float x = ((float)b0 - 127.0f) / 127.0f;
    float y = ((float)b1 - 127.0f) / 127.0f;
    float z = ((float)b2 - 127.0f) / 127.0f;
    // normalize (the packed form is already near-unit, but renormalize for safety)
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1e-8f)
    {
        x /= len;
        y /= len;
        z /= len;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

void unpackTexCoords(uint32_t packed, float out[2])
{
    out[0] = halfToFloat(static_cast<uint16_t>(packed >> 16));
    out[1] = halfToFloat(static_cast<uint16_t>(packed));
}

uint32_t packTangentFrame(const float normal[3], const float tangent[3], float binormalSign)
{
    const auto normalize = [](std::array<float, 3> &v) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!std::isfinite(length) || length < 1e-8f)
            throw std::runtime_error("Invalid model tangent basis");
        for (auto &component : v)
            component /= length;
    };
    std::array<float, 3> n{normal[0], normal[1], normal[2]};
    std::array<float, 3> t{tangent[0], tangent[1], tangent[2]};
    normalize(n);
    const float projection = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
    for (size_t axis = 0; axis < 3; ++axis)
        t[axis] -= projection * n[axis];
    normalize(t);
    if (!std::isfinite(binormalSign))
        throw std::runtime_error("Invalid model tangent handedness");
    const std::array<float, 3> b{n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2],
                                 n[0] * t[1] - n[1] * t[0]};
    const float m[3][3]{{t[0], b[0], n[0]}, {t[1], b[1], n[1]}, {t[2], b[2], n[2]}};
    std::array<float, 4> q{};
    const float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0)
    {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m[2][1] - m[1][2]) / scale, (m[0][2] - m[2][0]) / scale, (m[1][0] - m[0][1]) / scale,
             scale * 0.25f};
    }
    else
    {
        size_t axis = 0;
        for (size_t index = 1; index < 3; ++index)
            if (m[index][index] > m[axis][axis])
                axis = index;
        const size_t next = (axis + 1) % 3, last = (axis + 2) % 3;
        const float scale = std::sqrt(1.0f + m[axis][axis] - m[next][next] - m[last][last]) * 2.0f;
        q[axis] = scale * 0.25f;
        q[next] = (m[axis][next] + m[next][axis]) / scale;
        q[last] = (m[axis][last] + m[last][axis]) / scale;
        q[3] = (m[last][next] - m[next][last]) / scale;
    }

    size_t largest = 0;
    for (size_t axis = 1; axis < q.size(); ++axis)
        if (std::abs(q[axis]) > std::abs(q[largest]))
            largest = axis;
    const float sign = q[largest] < 0 ? -1.0f : 1.0f;
    uint32_t packed = static_cast<uint32_t>(largest) << 30;
    if (binormalSign < 0)
        packed |= 1u << 29;
    // Replay QuatDec3n stores the remaining components in 10/10/9 bits.
    constexpr float inverseRootTwo = 0.7071067811865475f;
    size_t component = 0;
    for (size_t axis = 0; axis < q.size(); ++axis)
    {
        if (axis == largest)
            continue;
        const uint32_t maximum = component == 2 ? 511u : 1023u;
        const float value = std::clamp(q[axis] * sign / inverseRootTwo, -1.0f, 1.0f);
        const auto quantized = static_cast<uint32_t>(std::lround((value + 1.0f) * 0.5f * maximum));
        packed |= quantized << (component++ * 10);
    }
    return packed;
}

uint32_t packTexCoords(const float uv[2])
{
    uint32_t u = floatToHalf(uv[0]);
    uint32_t v = floatToHalf(uv[1]);
    return (v << 16) | u;
}

uint64_t packPosition(const float pos[3], const float mid[3], const float half[3])
{
    // Replay unpacker RVA 0x1992D20 uses unsigned 21-bit components and one uniform scale.
    for (size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(pos[axis]) || !std::isfinite(mid[axis]) || !std::isfinite(half[axis]) ||
            half[axis] < 0)
            throw std::runtime_error("Invalid model position or bounds");
    const float scale = std::max({half[0], half[1], half[2]});
    auto enc = [](float p, float m, float h) -> uint64_t {
        const double c = h > 0 ? (static_cast<double>(p) - m) / h : 0.0;
        return static_cast<uint64_t>(
            std::llround((std::clamp(c, -1.0, 1.0) + 1.0) * 0.5 * 2097151.0));
    };
    uint64_t x = enc(pos[0], mid[0], scale);
    uint64_t y = enc(pos[1], mid[1], scale);
    uint64_t z = enc(pos[2], mid[2], scale);
    return (z << 42) | (y << 21) | x;
}

} // namespace xsurf_conv
