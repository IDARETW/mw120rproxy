#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace replaybounds
{
struct Bounds
{
    std::array<float, 3> midpoint{};
    std::array<float, 3> halfSize{};

    void Write(void *destination, float padding = 0) const
    {
        if (!std::isfinite(padding) || padding < 0)
            throw std::runtime_error("Invalid bounds padding");
        float values[6];
        for (unsigned k = 0; k < 3; ++k)
        {
            values[k] = midpoint[k];
            values[k + 3] = halfSize[k] + padding;
        }
        std::memcpy(destination, values, sizeof(values));
    }

    float Radius() const
    {
        double squared = 0;
        for (const float v : halfSize)
            squared += double(v) * v;
        return float(std::sqrt(squared));
    }
};

class Accumulator
{
    std::array<float, 3> mins{std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::infinity()};
    std::array<float, 3> maxs{-std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};
    bool populated = false;

  public:
    void Add(const std::array<float, 3> &point)
    {
        for (const float v : point)
            if (!std::isfinite(v) || std::abs(v) > 100000)
                throw std::runtime_error("Position outside Replay world bounds");
        for (unsigned k = 0; k < 3; ++k)
        {
            mins[k] = std::min(mins[k], point[k]);
            maxs[k] = std::max(maxs[k], point[k]);
        }
        populated = true;
    }

    Bounds Finish() const
    {
        if (!populated)
            throw std::runtime_error("Replay world has no non-sky geometry");
        Bounds result;
        for (unsigned k = 0; k < 3; ++k)
        {
            result.midpoint[k] = float((double(mins[k]) + maxs[k]) * .5);
            const double required = std::max(double(maxs[k]) - result.midpoint[k],
                                             double(result.midpoint[k]) - mins[k]);
            result.halfSize[k] = float(required);
            if (double(result.halfSize[k]) < required)
                result.halfSize[k] =
                    std::nextafter(result.halfSize[k], std::numeric_limits<float>::infinity());
        }
        return result;
    }
};
} // namespace replaybounds
