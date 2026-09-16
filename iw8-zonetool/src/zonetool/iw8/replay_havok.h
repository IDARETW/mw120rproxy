#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
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

struct CollisionSlab
{
    std::array<float, 3> direction{};
    float midpoint{};
    float halfSize{};
};

struct CollisionHull
{
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::uint32_t contents{};
    std::uint32_t model{};
    std::vector<CollisionSlab> slabs;
};

struct TriggerModel
{
    std::uint32_t collisionModel{};
    bool staticPhysics{true};
};

struct LadderFace
{
    std::array<float, 3> bottom{};
    std::array<float, 3> top{};
    std::array<float, 3> normal{};
    float width{};
};

struct BakeResult
{
    std::vector<std::uint8_t> world;
    std::vector<std::uint8_t> entities;
    std::vector<CollisionModel> models;
    std::vector<CollisionHull> hulls;
    std::vector<TriggerModel> triggers;
    std::vector<LadderFace> ladders;
};

struct PhysicsMesh
{
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::uint32_t contents{1};
};

struct PhysicsAsset
{
    std::string name;
    std::vector<std::uint8_t> havokData;
    std::uint32_t contents{1};
    std::uint32_t useCategory{3};
    std::uint32_t simulationCategory{1};
};

void PrepareCollisionBaker(const std::filesystem::path &replayExecutable);
bool FindOpaqueString(std::string_view value, std::uint32_t &id);
BakeResult BakeCollision(const BakeInput &input);
PhysicsAsset BakePhysicsAsset(std::string name, const PhysicsMesh &mesh,
                              std::uint32_t useCategory = 3,
                              std::uint32_t simulationCategory = 1);

} // namespace iw8::havok
