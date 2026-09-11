#pragma once

#include "common/json.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace iw3
{
enum class SurfaceKind : unsigned
{
    opaque,
    sky,
    glass,
    cutout,
    skipped,
};

struct MaterialPlan
{
    std::size_t tile{};
    SurfaceKind kind{SurfaceKind::opaque};
    unsigned flags{};
    std::array<float, 4> environment{0.8f, 4.0f, 2.5f, 0.625f};
};

struct LightmapRectangle
{
    unsigned x{};
    unsigned y{};
    unsigned width{};
    unsigned height{};
};

struct ReflectionProbePlan
{
    std::array<float, 3> origin{};
    nlohmann::json image;
    std::array<std::array<float, 9>, 4> sh{};
};

struct RenderPlan
{
    std::unordered_map<std::string, MaterialPlan> materials;
    std::vector<LightmapRectangle> lightmaps;
    std::array<std::size_t, 6> skyTiles{};
    unsigned columns{};
    bool hasCutout{};
    bool hasGlass{};
    std::string material;
    std::string materialDefinition;
    std::vector<nlohmann::json> additionalMaterials;
    std::vector<ReflectionProbePlan> reflectionProbes;
};

RenderPlan PrepareRenderAssets(const std::filesystem::path &exportRoot,
                               const nlohmann::json &world,
                               const std::vector<std::string> &surfaceMaterials,
                               const std::vector<std::filesystem::path> &sourcePaths,
                               const std::filesystem::path &mapDirectory,
                               const std::string &map);
} // namespace iw3
