#pragma once

#include "../convert/conv_xsurface.h"
#include "../dumpsrc/xmodel_dump.h"
#include "../iw8/replay_dynentity.h"
#include "../iw8/replay_havok.h"
#include "../iw8/replay_impact.h"
#include "../iw8/replay_render.h"
#include "../iw8/replay_sunshadow.h"
#include "../iw8/replay_vfx.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace iw3
{
struct ImportOptions
{
    std::filesystem::path fastfile;
    std::string map;
    std::filesystem::path unlinker;
    std::filesystem::path scratchRoot;
    std::vector<std::filesystem::path> searchPaths;
};

struct PreparedXModel
{
    convert::xmodel::Iw8XModelRecord model;
    std::vector<conv_xsurf::Iw8Surfs> lods;
    iw8::havok::PhysicsAsset physicsAsset;
};

struct PreparedFxDependency
{
    std::string name;
    std::string type;
};

struct PreparedFxFloatRange
{
    float base{};
    float amplitude{};
};

struct PreparedFxIntRange
{
    std::int32_t base{};
    std::int32_t amplitude{};
};

struct PreparedFxVec3Range
{
    std::array<float, 3> base{};
    std::array<float, 3> amplitude{};
};

struct PreparedFxSpawn
{
    std::array<std::int32_t, 2> raw{};
    std::int32_t loopingIntervalMsec{};
    std::int32_t loopingCount{};
    PreparedFxIntRange oneShotCount{};
};

struct PreparedFxAtlas
{
    std::uint8_t behavior{};
    std::uint8_t index{};
    std::uint8_t fps{};
    std::uint8_t loopCount{};
    std::uint8_t colIndexBits{};
    std::uint8_t rowIndexBits{};
    std::int16_t entryCount{};
};

struct PreparedFxVisualState
{
    std::array<std::uint8_t, 4> color{};
    float rotationDelta{};
    float rotationTotal{};
    std::array<float, 2> size{};
    float scale{};
};

struct PreparedFxVisualSample
{
    PreparedFxVisualState base{};
    PreparedFxVisualState amplitude{};
};

struct PreparedFxVelocityFrame
{
    PreparedFxVec3Range velocity{};
    PreparedFxVec3Range totalDelta{};
};

struct PreparedFxVelocitySample
{
    PreparedFxVelocityFrame local{};
    PreparedFxVelocityFrame world{};
};

struct PreparedFxTrailVertex
{
    std::array<float, 2> position{};
    std::array<float, 2> normal{};
    float texCoord{};
};

struct PreparedFxTrail
{
    std::int32_t scrollTimeMsec{};
    std::int32_t repeatDist{};
    std::int32_t splitDist{};
    std::vector<PreparedFxTrailVertex> vertices;
    std::vector<std::uint16_t> indices;
};

enum class PreparedFxVisualKind : std::uint8_t
{
    none,
    material,
    xmodel,
    effect,
    sound,
    decal,
};

struct PreparedFxVisual
{
    PreparedFxVisualKind kind{PreparedFxVisualKind::none};
    // Ordinary visuals have zero or one name. A decal has exactly two material
    // slots, with an empty string preserving a source null pointer.
    std::vector<std::string> names;
};

struct PreparedFxElement
{
    std::int32_t flags{};
    PreparedFxSpawn spawn{};
    PreparedFxFloatRange spawnRange{};
    PreparedFxFloatRange fadeInRange{};
    PreparedFxFloatRange fadeOutRange{};
    float spawnFrustumCullRadius{};
    PreparedFxIntRange spawnDelayMsec{};
    PreparedFxIntRange lifeSpanMsec{};
    std::array<PreparedFxFloatRange, 3> spawnOrigin{};
    PreparedFxFloatRange spawnOffsetRadius{};
    PreparedFxFloatRange spawnOffsetHeight{};
    std::array<PreparedFxFloatRange, 3> spawnAngles{};
    std::array<PreparedFxFloatRange, 3> angularVelocity{};
    PreparedFxFloatRange initialRotation{};
    PreparedFxFloatRange gravity{};
    PreparedFxFloatRange reflectionFactor{};
    PreparedFxAtlas atlas{};
    std::uint8_t type{};
    std::uint8_t visualCount{};
    std::uint8_t velocityIntervalCount{};
    std::uint8_t visualStateIntervalCount{};
    std::vector<PreparedFxVelocitySample> velocitySamples;
    std::vector<PreparedFxVisualSample> visualSamples;
    bool visualsPresent{};
    std::vector<PreparedFxVisual> visuals;
    std::array<float, 3> collisionMins{};
    std::array<float, 3> collisionMaxs{};
    std::string effectOnImpact;
    std::string effectOnDeath;
    std::string effectEmitted;
    PreparedFxFloatRange emitDist{};
    PreparedFxFloatRange emitDistVariance{};
    std::optional<PreparedFxTrail> trail;
    std::uint8_t sortOrder{};
    std::uint8_t lightingFrac{};
    std::uint8_t useItemClip{};
};

struct PreparedFx
{
    std::string name;
    std::filesystem::path sourcePath;
    std::int32_t flags{};
    std::int32_t totalSize{};
    std::int32_t msecLoopingLife{};
    std::array<std::uint32_t, 3> elementCounts{};
    std::vector<PreparedFxElement> elements;
    std::vector<PreparedFxDependency> dependencies;
};

struct PreparedRawFile
{
    std::string name;
    std::vector<std::uint8_t> data;
};

struct PreparedMap
{
    std::filesystem::path root;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
    std::filesystem::path scratch;
    std::vector<PreparedXModel> xmodels;
    std::vector<PreparedRawFile> rawFiles;
    std::vector<PreparedFx> fxEffects;
    std::unordered_map<std::string, std::string> fxMaterialAliases;
    std::vector<iw8::vfx::Effect> vfxEffects;
    std::string vfxLightDef;
    std::string smallGlassEffect;
    iw8::impact::EffectOverrides impactOverrides;
    replayrender::StaticModels staticModels;
    replaysunshadow::Scene shadowScene;
    std::vector<iw8::DynamicEntity> dynamicEntities;

    PreparedMap() = default;
    PreparedMap(const PreparedMap &) = delete;
    PreparedMap &operator=(const PreparedMap &) = delete;
    PreparedMap(PreparedMap &&other) noexcept;
    PreparedMap &operator=(PreparedMap &&other) noexcept;
    ~PreparedMap();
};

PreparedMap PrepareFastfile(const ImportOptions &options);
} // namespace iw3
