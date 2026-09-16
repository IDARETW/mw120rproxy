#include "common/ff_io.h"
#include "zonetool/iw8/replay_vfx.h"

#include <cstdint>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
        return 2;
    const std::string mode = argc == 3 ? argv[2] : "basic";
    const bool withModel = mode == "model";
    const bool rich = mode == "rich";
    const bool runnerMode = mode == "runner";
    const bool materialMode = mode == "material";
    if (mode != "basic" && !withModel && !rich && !runnerMode && !materialMode)
        return 2;
    iw8::ZoneWriter writer;
    const std::string modelName = "mw120r/vfx_probe_model";
    if (withModel)
    {
        writer.add(ASSET_TYPE_XMODEL, modelName,
                   [modelName](iw8::ZoneWriter &output) {
                       output.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
                       output.align(7);
                       std::array<uint8_t, iw8sz::XMODEL> root{};
                       const uint64_t namePointer = iw8::PTR_FOLLOWS;
                       std::memcpy(root.data(), &namePointer, sizeof(namePointer));
                       output.write(root.data(), root.size());
                       output.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
                       output.writeStr(modelName);
                       output.popStream();
                       output.popStream();
                   });
    }
    const std::string materialName = "mw120r/vfx_probe_material";
    if (materialMode)
    {
        writer.add(ASSET_TYPE_MATERIAL, materialName,
                   [materialName](iw8::ZoneWriter &output) {
                       output.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
                       output.align(7);
                       std::array<uint8_t, iw8sz::MATERIAL> root{};
                       const uint64_t namePointer = iw8::PTR_FOLLOWS;
                       std::memcpy(root.data(), &namePointer, sizeof(namePointer));
                       output.write(root.data(), root.size());
                       output.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
                       output.writeStr(materialName);
                       output.popStream();
                       output.popStream();
                   });
    }
    iw8::vfx::Effect effect;
    effect.name = "mw120r/vfx_contract_probe";
    effect.native.flags = 262163;
    effect.native.occlusionOverrideEmitterIndex = -1;
    effect.native.drawFrustumCullRadius = -1.0f;
    effect.native.updateFrustumCullRadius = -1.0f;
    effect.native.sunDistance = 100000.0f;

    iw8::vfx::Emitter emitter;
    emitter.native.particleSpawnRate = {5.0f, 5.0f};
    emitter.native.particleLife = {6.0f, 6.0f};
    emitter.native.particleCountMax = 5;
    emitter.native.particleBurstCount = {5, 5};
    emitter.native.flags = 2;
    iw8::vfx::State state;
    state.native.elementType = withModel ? 7 : 0;

    iw8::vfx::Module spawn;
    spawn.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initSpawn);
    iw8_focus::ParticleModuleInitSpawn spawnPayload{};
    spawnPayload.base.type = spawn.native.moduleType;
    spawnPayload.curve.scale = 1.0f;
    std::memcpy(spawn.native.moduleData, &spawnPayload, sizeof(spawnPayload));
    if (rich)
        spawn.spawnCurve.push_back({0.0f, 1.0f, 0.0f, 0});
    state.groups[0].push_back(spawn);

    iw8::vfx::Module attributes;
    attributes.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initAttributes);
    iw8_focus::ParticleModuleInitAttributes attributePayload{};
    attributePayload.base.type = attributes.native.moduleType;
    for (float &value : attributePayload.colorMin.v)
        value = 1.0f;
    for (float &value : attributePayload.colorMax.v)
        value = 1.0f;
    std::memcpy(attributes.native.moduleData, &attributePayload, sizeof(attributePayload));
    state.groups[0].push_back(attributes);

    if (withModel)
    {
        iw8::vfx::Module model;
        model.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initModel);
        iw8_focus::ParticleModuleInitModel modelPayload{};
        std::memcpy(&modelPayload.base, &model.native.moduleType, sizeof(model.native.moduleType));
        std::memcpy(model.native.moduleData, &modelPayload, sizeof(modelPayload));
        model.models.push_back(modelName);
        state.groups[0].push_back(model);
    }

    iw8::vfx::Module cloud;
    cloud.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initCloud);
    iw8_focus::ParticleModuleInitCloud cloudPayload{};
    cloudPayload.base.type = cloud.native.moduleType;
    std::memcpy(cloud.native.moduleData, &cloudPayload, sizeof(cloudPayload));
    state.groups[0].push_back(cloud);

    if (materialMode)
    {
        iw8::vfx::Module material;
        material.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initMaterial);
        iw8_focus::ParticleModuleInitMaterial materialPayload{};
        materialPayload.base.type = material.native.moduleType;
        std::memcpy(material.native.moduleData, &materialPayload, sizeof(materialPayload));
        material.materials.push_back(materialName);
        state.groups[1].push_back(std::move(material));

        iw8::vfx::Module decal;
        decal.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initDecal);
        iw8_focus::ParticleModuleInitDecal decalPayload{};
        decalPayload.base.type = decal.native.moduleType;
        std::memcpy(decal.native.moduleData, &decalPayload, sizeof(decalPayload));
        decal.decalMaterials.push_back({materialName, materialName, materialName});
        state.groups[2].push_back(std::move(decal));
    }

    if (rich)
    {
        iw8::vfx::Module color;
        color.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::colorGraph);
        iw8_focus::ParticleModuleColorGraph colorPayload{};
        colorPayload.base.type = color.native.moduleType;
        std::memcpy(color.native.moduleData, &colorPayload, sizeof(colorPayload));
        color.curves.resize(1);
        color.curves[0].points.push_back({0.0f, 1.0f, 0.0f, 0});
        color.curves[0].points.push_back({1.0f, 0.0f, 1.0f, 0});
        state.groups[1].push_back(std::move(color));

        iw8::vfx::Module drag;
        drag.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::forceDragGraph);
        iw8_focus::ParticleModuleForceDragGraph dragPayload{};
        dragPayload.base.type = drag.native.moduleType;
        std::memcpy(drag.native.moduleData, &dragPayload, sizeof(dragPayload));
        drag.curves.resize(1);
        drag.curves[0].points.push_back({0.0f, 0.25f, 0.0f, 0});
        state.groups[2].push_back(std::move(drag));
        emitter.fadeCurve.push_back({0.0f, 1.0f, 0.0f, 0});
    }

    emitter.states.push_back(state);
    effect.emitters.push_back(emitter);
    if (runnerMode)
    {
        iw8::vfx::Effect child = effect;
        child.name = "mw120r/vfx_contract_child";
        iw8::vfx::Register(writer, std::move(child));
        iw8::vfx::Module runner;
        runner.native.moduleType = static_cast<uint16_t>(iw8_focus::ParticleModuleType::initRunner);
        iw8_focus::ParticleModuleInitRunner runnerPayload{};
        runnerPayload.base.type = runner.native.moduleType;
        std::memcpy(runner.native.moduleData, &runnerPayload, sizeof(runnerPayload));
        runner.childEffects.push_back("mw120r/vfx_contract_child");
        effect.emitters[0].states[0].groups[1].push_back(std::move(runner));
        effect.name = "mw120r/vfx_contract_parent";
    }
    iw8::vfx::Register(writer, std::move(effect));
    writer.build();

    zt::Iw8WriteParams params;
    for (int index = 0; index < iw8::IW8_MAX_XFILE_COUNT; ++index)
    {
        params.blockSize[index] = writer.blockSize(index);
        params.totalDecompressed += params.blockSize[index];
    }
    params.calcSize = writer.calcSize();
    return zt::iw8_write(argv[1], writer.body(), params) ? 0 : 1;
}
