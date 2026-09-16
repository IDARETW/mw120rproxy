#include "replay_vfx.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace iw8::vfx
{
namespace
{
template <typename T> int32_t Count(const std::vector<T> &items)
{
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("Replay VFX array exceeds signed count range");
    return static_cast<int32_t>(items.size());
}

void StorePointer(void *record, const std::size_t offset, const uint64_t value)
{
    std::memcpy(static_cast<uint8_t *>(record) + offset, &value, sizeof(value));
}

template <std::size_t N>
void SetCurves(iw8_focus::ParticleCurveDef (&curves)[N], const std::vector<Module::Curve> &values)
{
    if (values.size() > N)
        throw std::runtime_error("Replay VFX module has too many curves");
    for (std::size_t index = 0; index < N; ++index)
    {
        curves[index].numControlPoints = index < values.size() ? Count(values[index].points) : 0;
        StorePointer(&curves[index], 0,
                     curves[index].numControlPoints ? PTR_FOLLOWS : PTR_NULL);
    }
}

void WriteCurves(ZoneWriter &writer, const std::vector<Module::Curve> &curves)
{
    for (const auto &curve : curves)
    {
        if (curve.points.empty())
            continue;
        writer.align(15);
        writer.write(curve.points.data(), curve.points.size() *
                                            sizeof(iw8_focus::ParticleCurveControlPointDef));
    }
}

void WriteModules(ZoneWriter &writer, const std::vector<Module> &modules)
{
    if (modules.empty())
        return;
    writer.align(15);
    for (const auto &module : modules)
    {
        auto record = module.native;
        if (record.moduleType != static_cast<uint16_t>(iw8_focus::ParticleModuleType::initRunner) &&
            !module.childEffects.empty())
            throw std::runtime_error("Replay VFX child effect list belongs only to INIT_RUNNER");
        switch (static_cast<iw8_focus::ParticleModuleType>(record.moduleType))
        {
        case iw8_focus::ParticleModuleType::initSpawn:
        {
            if (!module.models.empty() || !module.materials.empty() ||
                !module.decalMaterials.empty() || !module.curves.empty())
                throw std::runtime_error("Replay VFX spawn module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitSpawn payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            payload.curve.numControlPoints = Count(module.spawnCurve);
            StorePointer(&payload, offsetof(iw8_focus::ParticleModuleInitSpawn, curve),
                         payload.curve.numControlPoints ? PTR_FOLLOWS : PTR_NULL);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initModel:
        {
            if (!module.spawnCurve.empty() || !module.materials.empty() ||
                !module.decalMaterials.empty() || !module.curves.empty())
                throw std::runtime_error("Replay VFX model module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitModel payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            payload.linkedAssets.numAssets = Count(module.models);
            StorePointer(&payload, offsetof(iw8_focus::ParticleModuleInitModel, linkedAssets),
                         payload.linkedAssets.numAssets ? PTR_FOLLOWS : PTR_NULL);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initMaterial:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.decalMaterials.empty() || !module.curves.empty())
                throw std::runtime_error("Replay VFX material module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitMaterial payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            payload.linkedAssets.numAssets = Count(module.materials);
            StorePointer(&payload, offsetof(iw8_focus::ParticleModuleInitMaterial, linkedAssets),
                         payload.linkedAssets.numAssets ? PTR_FOLLOWS : PTR_NULL);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initDecal:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.curves.empty())
                throw std::runtime_error("Replay VFX decal module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitDecal payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            payload.linkedAssets.numAssets = Count(module.decalMaterials);
            StorePointer(&payload, offsetof(iw8_focus::ParticleModuleInitDecal, linkedAssets),
                         payload.linkedAssets.numAssets ? PTR_FOLLOWS : PTR_NULL);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initRunner:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty() ||
                !module.curves.empty())
                throw std::runtime_error("Replay VFX runner module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitRunner payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            payload.linkedAssets.numAssets = Count(module.childEffects);
            StorePointer(&payload, offsetof(iw8_focus::ParticleModuleInitRunner, linkedAssets),
                         payload.linkedAssets.numAssets ? PTR_FOLLOWS : PTR_NULL);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initAtlas:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX atlas module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitAtlas payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::colorGraph:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX color graph has unrelated pointer assets");
            iw8_focus::ParticleModuleColorGraph payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::sizeGraph:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX size graph has unrelated pointer assets");
            iw8_focus::ParticleModuleSizeGraph payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initSpawnShapeCylinder:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX cylinder module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitSpawnShapeCylinder payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initSpawnShapeSphere:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX sphere module has unrelated pointer assets");
            iw8_focus::ParticleModuleInitSpawnShapeSphere payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::forceDragGraph:
        {
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty())
                throw std::runtime_error("Replay VFX force drag module has unrelated pointer assets");
            iw8_focus::ParticleModuleForceDragGraph payload{};
            std::memcpy(&payload, record.moduleData, sizeof(payload));
            SetCurves(payload.curves, module.curves);
            std::memcpy(record.moduleData, &payload, sizeof(payload));
            break;
        }
        case iw8_focus::ParticleModuleType::initAttributes:
        case iw8_focus::ParticleModuleType::initCloud:
        case iw8_focus::ParticleModuleType::initTail:
        case iw8_focus::ParticleModuleType::initOrientedSprite:
        case iw8_focus::ParticleModuleType::initMirrorTexture:
        case iw8_focus::ParticleModuleType::initRelativeVelocity:
        case iw8_focus::ParticleModuleType::initRotation:
        case iw8_focus::ParticleModuleType::initRotation3D:
        case iw8_focus::ParticleModuleType::gravity:
            if (!module.spawnCurve.empty() || !module.models.empty() ||
                !module.materials.empty() || !module.decalMaterials.empty() ||
                !module.curves.empty())
                throw std::runtime_error("Replay VFX value module has pointer assets");
            break;
        default:
            throw std::runtime_error("Replay VFX module selector has no pinned writer layout");
        }
        writer.writeT(record);
    }

    for (const auto &module : modules)
    {
        switch (static_cast<iw8_focus::ParticleModuleType>(module.native.moduleType))
        {
        case iw8_focus::ParticleModuleType::initSpawn:
            if (!module.spawnCurve.empty())
            {
                writer.align(15);
                writer.write(module.spawnCurve.data(), module.spawnCurve.size() *
                                                        sizeof(iw8_focus::ParticleCurveControlPointDef));
            }
            break;
        case iw8_focus::ParticleModuleType::initMaterial:
            if (!module.materials.empty())
            {
                writer.align(7);
                for (const auto &name : module.materials)
                {
                    iw8_focus::ParticleLinkedAssetDef linked{};
                    StorePointer(&linked, 0, writer.assetAlias(ASSET_TYPE_MATERIAL, name));
                    writer.writeT(linked);
                }
            }
            break;
        case iw8_focus::ParticleModuleType::initDecal:
            if (!module.decalMaterials.empty())
            {
                writer.align(7);
                for (const auto &names : module.decalMaterials)
                {
                    iw8_focus::ParticleLinkedAssetDef linked{};
                    for (std::size_t index = 0; index < names.size(); ++index)
                        StorePointer(&linked, index * sizeof(uint64_t),
                                     names[index].empty() ? PTR_NULL
                                                          : writer.assetAlias(ASSET_TYPE_MATERIAL,
                                                                              names[index]));
                    writer.writeT(linked);
                }
            }
            break;
        case iw8_focus::ParticleModuleType::initRunner:
            if (!module.childEffects.empty())
            {
                writer.align(7);
                for (const auto &name : module.childEffects)
                {
                    iw8_focus::ParticleLinkedAssetDef linked{};
                    StorePointer(&linked, 0, writer.assetAlias(ASSET_TYPE_VFX, name));
                    writer.writeT(linked);
                }
            }
            break;
        case iw8_focus::ParticleModuleType::initAtlas:
        case iw8_focus::ParticleModuleType::colorGraph:
        case iw8_focus::ParticleModuleType::sizeGraph:
        case iw8_focus::ParticleModuleType::initSpawnShapeCylinder:
        case iw8_focus::ParticleModuleType::initSpawnShapeSphere:
        case iw8_focus::ParticleModuleType::forceDragGraph:
            WriteCurves(writer, module.curves);
            break;
        case iw8_focus::ParticleModuleType::initModel:
            if (!module.models.empty())
            {
                writer.align(7);
                for (const auto &name : module.models)
                {
                    iw8_focus::ParticleLinkedAssetDef linked{};
                    StorePointer(&linked, 0, writer.assetAlias(ASSET_TYPE_XMODEL, name));
                    writer.writeT(linked);
                }
            }
            break;
        default:
            break;
        }
    }
}

void WriteBody(ZoneWriter &writer, const Effect &effect)
{
    if (effect.name.empty())
        throw std::runtime_error("Replay VFX has no asset name");
    auto root = effect.native;
    root.version = 23;
    root.numEmitters = Count(effect.emitters);
    root.numScriptedInputNodes = 0;
    StorePointer(&root, offsetof(iw8_focus::ParticleSystemDef, name), PTR_FOLLOWS);
    StorePointer(&root, offsetof(iw8_focus::ParticleSystemDef, emitterDefs),
                 root.numEmitters ? PTR_FOLLOWS : PTR_NULL);
    StorePointer(&root, offsetof(iw8_focus::ParticleSystemDef, scriptedInputNodeDefs), PTR_NULL);

    writer.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    writer.align(15);
    writer.writeT(root);
    writer.pushStream(XFILE_BLOCK_VIRTUAL);
    writer.writeStr(effect.name);

    if (!effect.emitters.empty())
    {
        writer.align(15);
        for (const auto &emitter : effect.emitters)
        {
            auto record = emitter.native;
            record.numStates = Count(emitter.states);
            StorePointer(&record, offsetof(iw8_focus::ParticleEmitterDef, stateDefs),
                         record.numStates ? PTR_FOLLOWS : PTR_NULL);
            record.fadeCurveDef.numControlPoints = Count(emitter.fadeCurve);
            StorePointer(&record, offsetof(iw8_focus::ParticleEmitterDef, fadeCurveDef),
                         record.fadeCurveDef.numControlPoints ? PTR_FOLLOWS : PTR_NULL);
            writer.writeT(record);
        }

        for (const auto &emitter : effect.emitters)
        {
            if (!emitter.states.empty())
            {
                writer.align(15);
                for (const auto &state : emitter.states)
                {
                    auto record = state.native;
                    StorePointer(&record, offsetof(iw8_focus::ParticleStateDef, moduleGroupDefs),
                                 PTR_FOLLOWS);
                    writer.writeT(record);
                }
                for (const auto &state : emitter.states)
                {
                    writer.align(15);
                    for (const auto &modules : state.groups)
                    {
                        iw8_focus::ParticleModuleGroupDef group{};
                        group.numModules = Count(modules);
                        StorePointer(&group, 0, group.numModules ? PTR_FOLLOWS : PTR_NULL);
                        writer.writeT(group);
                    }
                    for (const auto &modules : state.groups)
                        WriteModules(writer, modules);
                }
            }
            if (!emitter.fadeCurve.empty())
            {
                writer.align(15);
                writer.write(emitter.fadeCurve.data(), emitter.fadeCurve.size() *
                                                        sizeof(iw8_focus::ParticleCurveControlPointDef));
            }
        }
    }
    writer.popStream();
    writer.popStream();
}
} // namespace

void Register(ZoneWriter &writer, Effect effect)
{
    const auto name = effect.name;
    writer.add(ASSET_TYPE_VFX, name,
               [effect = std::move(effect)](ZoneWriter &output) { WriteBody(output, effect); });
}
} // namespace iw8::vfx
