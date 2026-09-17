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
    unsigned worldMaterialIndex{};
    std::string modelMaterial;
    std::string glassMaterial;
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
    std::size_t sourceIndex{};
    std::array<float, 3> origin{};
    nlohmann::json image;
    std::array<std::array<float, 9>, 4> sh{};
};

struct NativeLightmapPlan
{
    unsigned width{};
    unsigned height{};
    std::array<unsigned, 3> formats{39u, 32u, 40u};
    std::array<std::string, 3> files;
};

struct RenderPlan
{
    std::unordered_map<std::string, MaterialPlan> materials;
    std::unordered_map<std::string, std::string> fxMaterialAliases;
    std::vector<LightmapRectangle> lightmaps;
    std::array<std::size_t, 6> skyTiles{};
    unsigned columns{};
    bool hasCutout{};
    bool hasGlass{};
    std::string material;
    std::string materialDefinition;
    std::vector<nlohmann::json> additionalMaterials;
    std::vector<nlohmann::json> assetMaterials;
    std::vector<ReflectionProbePlan> reflectionProbes;
    nlohmann::json reflectionProbeArrayImage;
    std::vector<NativeLightmapPlan> nativeLightmaps;
};

RenderPlan PrepareRenderAssets(const std::filesystem::path &exportRoot,
                               const nlohmann::json &world,
                               const std::vector<std::string> &surfaceMaterials,
                               const std::vector<std::string> &worldMaterials,
                               const std::vector<std::string> &modelMaterials,
                               const std::vector<std::string> &fxMaterials,
                               const std::vector<std::filesystem::path> &sourcePaths,
                               const std::filesystem::path &mapDirectory,
                               const std::string &map);
} // namespace iw3
