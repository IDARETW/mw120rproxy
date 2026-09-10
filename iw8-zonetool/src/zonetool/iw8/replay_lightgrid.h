#pragma once
#include "iw8_zone.h"
#include <array>
#include <string>
#include <vector>

namespace replaylightgrid
{
struct Grid
{
    std::array<uint32_t, 4> counts{};
    std::vector<uint8_t> payload;
    explicit operator bool() const
    {
        return !payload.empty();
    }
};
Grid Load(const std::string &meshPath);
void Emit(iw8::ZoneWriter &writer, const Grid &grid);
} // namespace replaylightgrid
