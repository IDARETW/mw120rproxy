#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

namespace iw8::havok
{

struct BakeInput
{
    std::filesystem::path replayExecutable;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
};

struct CollisionModel
{
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::uint32_t hullCount{};
    std::uint16_t shapeIndex{std::numeric_limits<std::uint16_t>::max()};
};

struct BakeResult
{
    std::vector<std::uint8_t> world;
    std::vector<std::uint8_t> entities;
    std::vector<CollisionModel> models;
};

void PrepareCollisionBaker(const std::filesystem::path &replayExecutable);
BakeResult BakeCollision(const BakeInput &input);

} // namespace iw8::havok
