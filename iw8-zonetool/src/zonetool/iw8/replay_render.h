#pragma once
#include "iw8_zone.h"
#include "replay_bounds.h"
#include <array>
#include <string>
#include <vector>
namespace replayrender
{
struct Shader
{
    unsigned type;
    std::string name, debugName;
    std::vector<uint8_t> header, program;
};
struct Image
{
    std::string name;
    uint16_t width{}, height{};
    unsigned mipCount = 1, format = 7;
    uint32_t flags = 0;
    uint16_t depth = 1, numElements = 1;
    uint8_t semantic = 1, category = 1;
    std::vector<uint8_t> pixels;
};
struct Technique
{
    std::string name;
    std::vector<uint8_t> header, states, rootsig, statebits, args;
    std::array<std::string, 4> shaders;
};
struct DecalVolumeMaterial
{
    std::string name;
    std::array<std::string, 6> channels;
    uint32_t flags{};
    std::array<float, 3> colorTint{1.0f, 1.0f, 1.0f};
    float alphaDissolveParms{};
    float emissiveScale{1.0f};
    uint32_t packedDisplacementScaleAndBias{3154132992u};
    float displacementCutoffDistance{750.0f};
    float displacementCutoffFalloff{0.004f};
    uint32_t packedTemperatureBaseAndScale{48128u};
    uint8_t rows{1};
    uint8_t columns{1};
};
struct Material
{
    std::string material;
    std::vector<uint8_t> materialInfo, constants, bufferIndices, textureHeaders;
    std::string techset;
    std::vector<uint8_t> techsetHeader;
    std::vector<Shader> shaders;
    std::vector<Technique> techniques;
    std::vector<std::string> images;
    std::string decalVolumeMaterial;
    DecalVolumeMaterial decalVolumeDefinition;
    std::vector<Image> imageDefinitions;
    std::vector<std::array<std::vector<uint8_t>, 4>> buffers;
};
struct BrushModel
{
    replaybounds::Bounds bounds{};
    unsigned firstSurface = 0;
    unsigned surfaceCount = 0;
};
struct DpvsPlane
{
    std::array<float, 3> normal{};
    float distance{};
    uint8_t type{};
};
struct Portal
{
    std::array<float, 4> plane{};
    unsigned cell{};
    std::vector<std::array<float, 3>> vertices;
    std::array<std::array<float, 3>, 2> hullAxis{};
};
struct CellTree
{
    replaybounds::Bounds bounds{};
    unsigned firstSurface{};
    unsigned surfaceCount{};
    unsigned childrenOffset{};
    uint16_t childCount{};
    std::vector<uint16_t> staticModelIndexes;
};
struct Cell
{
    replaybounds::Bounds bounds{};
    std::vector<Portal> portals;
    std::vector<CellTree> trees;
};
struct ReflectionProbe
{
    std::array<float, 3> origin{};
    replaybounds::Bounds volume{};
    Image image;
    std::array<std::array<float, 9>, 4> sh{};
};
struct NativeLightmapImage
{
    uint32_t format{};
    uint16_t width{};
    uint16_t height{};
    std::vector<uint8_t> pixels;
};
struct NativeLightmap
{
    std::array<NativeLightmapImage, 3> images;
};
struct GlassPane
{
    std::string material;
    std::string materialShattered;
    std::array<float, 3> origin{};
    std::array<float, 4> quaternion{};
    std::array<float, 4> texVecs{};
    std::array<float, 2> texCoordOrigin{};
    float halfWidth{};
    float halfHeight{};
    float halfThickness{};
};
struct StaticModelSurface
{
    replaybounds::Bounds bounds{};
    std::string material;
};
struct StaticModelLod
{
    float distance{};
    std::vector<StaticModelSurface> surfaces;
};
struct StaticModel
{
    std::string name;
    replaybounds::Bounds bounds{};
    std::vector<StaticModelLod> lods;
};
struct StaticModelInstance
{
    unsigned model{};
    std::array<float, 3> origin{};
    std::array<float, 4> quaternion{0, 0, 0, 1};
    float scale{1};
};
struct StaticModels
{
    std::vector<StaticModel> models;
    std::vector<StaticModelInstance> instances;
};
struct Mesh : Material
{
    replaybounds::Bounds sceneBounds{{0, 0, 0}, {100000, 100000, 100000}};
    replaybounds::Bounds drawBounds{{0, 0, 0}, {100000, 100000, 100000}};
    std::vector<Material> additionalMaterials;
    std::vector<Material> assetMaterials;
    std::vector<BrushModel> brushModels;
    std::vector<DpvsPlane> planes;
    std::vector<uint16_t> nodes;
    std::vector<Cell> cells;
    std::vector<ReflectionProbe> reflectionProbes;
    Image reflectionProbeArrayImage;
    std::vector<NativeLightmap> nativeLightmaps;
    std::vector<GlassPane> glassPanes;
    std::vector<unsigned> surfaceMaterials;
    unsigned opaqueCount = 0;
    std::vector<uint8_t> surfaces, bounds, drawSurfs, surfData, positions, aux, indices;
    unsigned count = 0;
    unsigned words() const
    {
        return (count + 31) / 32;
    }
    unsigned worldSurfaceCount() const
    {
        return brushModels.empty() ? count : brushModels.front().surfaceCount;
    }
};
Mesh Load(const std::string &path);
void RegisterMaterial(iw8::ZoneWriter &writer, const Mesh &mesh);
void RegisterMapMaterials(iw8::ZoneWriter &main, iw8::ZoneWriter &techsets,
                          const Mesh &mesh);
void RegisterMaterialReference(iw8::ZoneWriter &writer, const std::string &name);
void RegisterReflectionProbeImage(iw8::ZoneWriter &writer, const Mesh &mesh);
void StampWorld(std::vector<uint8_t> &world, const Mesh &mesh);
void EmitSurfaces(iw8::ZoneWriter &writer, const Mesh &mesh);
void StampTransient(uint8_t *transient, const Mesh &mesh);
void EmitVertices(iw8::ZoneWriter &writer, const Mesh &mesh);
void EmitSortedSurfaces(iw8::ZoneWriter &writer, const Mesh &mesh,
                        const StaticModels &staticModels);
void StampStaticModels(std::vector<uint8_t> &world, const StaticModels &models);
void EmitStaticModels(iw8::ZoneWriter &writer, const StaticModels &models,
                      unsigned lastSunPrimaryLightIndex);
void StampReflectionProbes(std::vector<uint8_t> &world, const Mesh &mesh,
                           const iw8::ZoneWriter &writer);
void EmitReflectionProbes(iw8::ZoneWriter &writer, const Mesh &mesh);
std::vector<uint8_t> BuildUmbraTome(const Mesh &mesh, const StaticModels &staticModels);
} // namespace replayrender
