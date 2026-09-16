#pragma once

#include "../convert/conv_xsurface.h"
#include "../dumpsrc/xmodel_dump.h"
#include "../iw8/replay_dynentity.h"
#include "../iw8/replay_havok.h"
#include "../iw8/replay_render.h"

#include <filesystem>
#include <string>
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

struct PreparedFx
{
    std::string name;
    std::filesystem::path sourcePath;
    std::vector<PreparedFxDependency> dependencies;
};

struct PreparedMap
{
    std::filesystem::path root;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
    std::filesystem::path scratch;
    std::vector<PreparedXModel> xmodels;
    std::vector<PreparedFx> fxEffects;
    replayrender::StaticModels staticModels;
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
