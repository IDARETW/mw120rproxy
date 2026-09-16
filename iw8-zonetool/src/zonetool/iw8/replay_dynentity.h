#pragma once

#include "iw8_zone.h"

#include <array>
#include <string>
#include <vector>

namespace iw8
{
enum class DynamicEntityBasis : std::uint8_t
{
    model = 0,
    brush = 1,
};

struct DynamicEntity
{
    std::string model;
    std::array<float, 4> quaternion{};
    std::array<float, 3> origin{};
    bool noPhysics{true};
    DynamicEntityBasis basis{DynamicEntityBasis::model};
    std::uint16_t brushModel{};
};

struct DynamicEntityCounts
{
    std::uint32_t models{};
    std::uint32_t brushes{};
};

DynamicEntityCounts CountDynamicEntities(const std::vector<DynamicEntity> &entities);
void RegisterDynamicEntityList(ZoneWriter &writer, const std::vector<DynamicEntity> &entities);
} // namespace iw8
