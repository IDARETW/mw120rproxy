// Extension for OpenAssetTools v0.33.0 (GPL-3.0). Offline IW3 intermediate export.
// Install with configure_exporters.ps1 before building Unlinker.
#pragma once
#include "Dumping/AbstractAssetDumper.h"
#include "Game/IW3/CommonIW3.h"
#include "Game/IW3/IW3.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace replay_export
{
using Json = nlohmann::json;

inline Json V3(const float *p)
{
    return Json::array({p[0], p[1], p[2]});
}

inline Json Normal(const IW3::PackedUnitVec &p)
{
    float n[3];
    IW3::Common::Vec3UnpackUnitVec(p, n);
    return V3(n);
}

inline void Save(AssetDumpingContext &context, const std::string &path, const Json &data)
{
    auto file = context.OpenAssetFile(path);
    if (!file)
        throw std::runtime_error("Cannot write Replay intermediate");
    *file << data.dump();
}

namespace fx
{
// These limits bound the source-side intermediate. They are deliberately
// independent of Replay's target asset limits; this dumper does not claim an
// IW3-to-Replay VFX mapping.
constexpr size_t kMaxNameLength = 1024;
constexpr size_t kMaxPathLength = 256;
constexpr size_t kMaxPathComponentLength = 64;
constexpr size_t kMaxElementCount = 4096;
constexpr size_t kMaxSampleCount = 256;
constexpr size_t kMaxVisualCount = 256;
constexpr size_t kMaxTrailVertexCount = 16384;
constexpr size_t kMaxTrailIndexCount = 16384;
constexpr size_t kMaxDependencyCount = 8192;

static_assert(sizeof(IW3::FxEffectDef) == 32);
static_assert(sizeof(IW3::FxElemDef) == 252);
static_assert(sizeof(IW3::FxTrailDef) == 28);

inline std::string BoundedString(const char *value, const char *field)
{
    if (!value)
        return {};

    size_t length = 0;
    while (length < kMaxNameLength)
    {
        if (value[length] == '\0')
            return std::string(value, length);
        ++length;
    }
    throw std::runtime_error(std::string("IW3 FX ") + field + " exceeds the string limit");
}

inline std::string AssetName(const char *value, const char *field)
{
    auto name = BoundedString(value, field);
    // OpenAssetTools prefixes reference-only XAsset names with ','. The
    // marker is zone metadata, not part of the asset's resolvable name.
    if (!name.empty() && name.front() == ',')
        name.erase(name.begin());
    if (name.empty())
        throw std::runtime_error(std::string("Invalid IW3 FX ") + field);
    return name;
}

inline bool IsSafePathCharacter(const char value)
{
    const auto c = static_cast<unsigned char>(value);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::string_view("_-.+$~&,()'[]=@^{}!").find(value) != std::string_view::npos;
}

inline std::string ValidateAssetPath(const char *value)
{
    const auto name = BoundedString(value, "asset name");
    if (name.empty() || name.size() > kMaxPathLength || name.front() == '/' ||
        name.back() == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos)
        throw std::runtime_error("Invalid IW3 FX asset path");

    size_t componentStart = 0;
    while (componentStart < name.size())
    {
        const auto slash = name.find('/', componentStart);
        const auto componentEnd = slash == std::string::npos ? name.size() : slash;
        const auto componentLength = componentEnd - componentStart;
        if (componentLength == 0 || componentLength > kMaxPathComponentLength)
            throw std::runtime_error("Invalid IW3 FX asset path component");

        const auto component = std::string_view(name).substr(componentStart, componentLength);
        if (component == "." || component == "..")
            throw std::runtime_error("Invalid IW3 FX asset path component");
        for (const auto character : component)
            if (!IsSafePathCharacter(character))
                throw std::runtime_error("Invalid character in IW3 FX asset path");

        if (slash == std::string::npos)
            break;
        componentStart = slash + 1;
    }
    return name;
}

inline size_t CheckedCount(const int value, const size_t limit, const char *field)
{
    if (value < 0 || static_cast<size_t>(value) > limit)
        throw std::runtime_error(std::string("Invalid IW3 FX ") + field + " count");
    return static_cast<size_t>(value);
}

inline size_t SampleCount(const char intervalCount, const void *samples, const char *field)
{
    const auto interval = static_cast<unsigned char>(intervalCount);
    if (!samples)
    {
        if (interval != 0)
            throw std::runtime_error(std::string("Missing IW3 FX ") + field + " samples");
        return 0;
    }

    const auto count = static_cast<size_t>(interval) + 1u;
    if (count > kMaxSampleCount)
        throw std::runtime_error(std::string("Invalid IW3 FX ") + field + " sample count");
    return count;
}

struct Dependencies
{
    std::set<std::pair<std::string, std::string>> names;

    void Add(const char *type, const std::string &name)
    {
        if (name.empty())
            return;

        const auto key = std::make_pair(std::string(type), name);
        if (names.find(key) == names.end() && names.size() >= kMaxDependencyCount)
            throw std::runtime_error("IW3 FX dependency count exceeds the limit");
        names.insert(key);
    }

    void Add(const char *type, const char *name)
    {
        if (name)
            Add(type, AssetName(name, "dependency name"));
    }

    Json Write() const
    {
        Json result = Json::array();
        for (const auto &key : names)
            result.push_back({{"type", key.first}, {"name", key.second}});
        return result;
    }
};

inline Json IntRange(const IW3::FxIntRange &range)
{
    return {{"base", range.base}, {"amplitude", range.amplitude}};
}

inline Json FloatRange(const IW3::FxFloatRange &range)
{
    return {{"base", range.base}, {"amplitude", range.amplitude}};
}

inline Json Vec3Range(const IW3::FxElemVec3Range &range)
{
    return {{"base", {range.base[0], range.base[1], range.base[2]}},
            {"amplitude", {range.amplitude[0], range.amplitude[1], range.amplitude[2]}}};
}

inline Json Spawn(const IW3::FxSpawnDef &spawn)
{
    // FxSpawnDef is a source union. Keep both views and the two raw words so
    // the intermediate does not silently choose the wrong element mode.
    return {{"raw", {spawn.looping.intervalMsec, spawn.looping.count}},
            {"looping", {{"interval_msec", spawn.looping.intervalMsec},
                          {"count", spawn.looping.count}}},
            {"one_shot", {{"count", IntRange(spawn.oneShot.count)}}}};
}

inline Json Atlas(const IW3::FxElemAtlas &atlas)
{
    return {{"behavior", static_cast<unsigned char>(atlas.behavior)},
            {"index", static_cast<unsigned char>(atlas.index)},
            {"fps", static_cast<unsigned char>(atlas.fps)},
            {"loop_count", static_cast<unsigned char>(atlas.loopCount)},
            {"col_index_bits", static_cast<unsigned char>(atlas.colIndexBits)},
            {"row_index_bits", static_cast<unsigned char>(atlas.rowIndexBits)},
            {"entry_count", atlas.entryCount}};
}

inline Json VisualState(const IW3::FxElemVisualState &state)
{
    return {{"color", {static_cast<unsigned char>(state.color[0]),
                        static_cast<unsigned char>(state.color[1]),
                        static_cast<unsigned char>(state.color[2]),
                        static_cast<unsigned char>(state.color[3])}},
            {"rotation_delta", state.rotationDelta},
            {"rotation_total", state.rotationTotal},
            {"size", {state.size[0], state.size[1]}},
            {"scale", state.scale}};
}

inline Json VisualSample(const IW3::FxElemVisStateSample &sample)
{
    return {{"base", VisualState(sample.base)}, {"amplitude", VisualState(sample.amplitude)}};
}

inline Json VelocityFrame(const IW3::FxElemVelStateInFrame &frame)
{
    return {{"velocity", Vec3Range(frame.velocity)},
            {"total_delta", Vec3Range(frame.totalDelta)}};
}

inline Json VelocitySample(const IW3::FxElemVelStateSample &sample)
{
    return {{"local", VelocityFrame(sample.local)}, {"world", VelocityFrame(sample.world)}};
}

inline Json Trail(const IW3::FxTrailDef &trail)
{
    const auto vertexCount = CheckedCount(trail.vertCount, kMaxTrailVertexCount, "trail vertex");
    const auto indexCount = CheckedCount(trail.indCount, kMaxTrailIndexCount, "trail index");
    if ((vertexCount && !trail.verts) || (indexCount && !trail.inds))
        throw std::runtime_error("Missing IW3 FX trail payload");

    Json vertices = Json::array();
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const auto &vertex = trail.verts[i];
        vertices.push_back({{"position", {vertex.pos[0], vertex.pos[1]}},
                            {"normal", {vertex.normal[0], vertex.normal[1]}},
                            {"tex_coord", vertex.texCoord}});
    }

    Json indices = Json::array();
    for (size_t i = 0; i < indexCount; ++i)
        indices.push_back(trail.inds[i]);

    return {{"scroll_time_msec", trail.scrollTimeMsec},
            {"repeat_dist", trail.repeatDist},
            {"split_dist", trail.splitDist},
            {"vertices", std::move(vertices)},
            {"indices", std::move(indices)}};
}

inline const char *VisualType(const unsigned char elemType)
{
    switch (elemType)
    {
    case IW3::FX_ELEM_TYPE_SPRITE_BILLBOARD:
        return "sprite_billboard";
    case IW3::FX_ELEM_TYPE_SPRITE_ORIENTED:
        return "sprite_oriented";
    case IW3::FX_ELEM_TYPE_TAIL:
        return "tail";
    case IW3::FX_ELEM_TYPE_TRAIL:
        return "trail";
    case IW3::FX_ELEM_TYPE_CLOUD:
        return "cloud";
    case IW3::FX_ELEM_TYPE_MODEL:
        return "model";
    case IW3::FX_ELEM_TYPE_OMNI_LIGHT:
        return "omni_light";
    case IW3::FX_ELEM_TYPE_SPOT_LIGHT:
        return "spot_light";
    case IW3::FX_ELEM_TYPE_SOUND:
        return "sound";
    case IW3::FX_ELEM_TYPE_DECAL:
        return "decal";
    case IW3::FX_ELEM_TYPE_RUNNER:
        return "runner";
    default:
        return "unknown";
    }
}

inline Json EffectRef(const IW3::FxEffectDefRef &ref, Dependencies &dependencies)
{
    if (!ref.name)
        return nullptr;
    const auto name = AssetName(ref.name, "effect reference");
    dependencies.Add("fx", name);
    return name;
}

inline Json Visual(const IW3::FxElemVisuals &visual, const unsigned char elemType,
                   Dependencies &dependencies)
{
    if (elemType <= IW3::FX_ELEM_TYPE_CLOUD)
    {
        if (!visual.material || !visual.material->info.name)
            throw std::runtime_error("Missing IW3 FX material visual");
        const auto name = AssetName(visual.material->info.name, "material name");
        dependencies.Add("material", name);
        return {{"type", "material"}, {"name", name}};
    }
    if (elemType == IW3::FX_ELEM_TYPE_MODEL)
    {
        if (!visual.model || !visual.model->name)
            throw std::runtime_error("Missing IW3 FX model visual");
        const auto name = AssetName(visual.model->name, "model name");
        dependencies.Add("xmodel", name);
        return {{"type", "xmodel"}, {"name", name}};
    }
    if (elemType == IW3::FX_ELEM_TYPE_RUNNER)
        return {{"type", "fx"}, {"name", EffectRef(visual.effectDef, dependencies)}};
    if (elemType == IW3::FX_ELEM_TYPE_SOUND)
    {
        if (!visual.soundName)
            return {{"type", "sound"}, {"name", nullptr}};
        const auto name = AssetName(visual.soundName, "sound name");
        dependencies.Add("sound", name);
        return {{"type", "sound"}, {"name", name}};
    }
    return {{"type", "none"}};
}

inline Json MarkVisual(const IW3::FxElemMarkVisuals &visuals, Dependencies &dependencies)
{
    Json materials = Json::array();
    for (const auto *material : visuals.materials)
    {
        if (!material)
        {
            materials.push_back(nullptr);
            continue;
        }
        if (!material->info.name)
            throw std::runtime_error("Missing IW3 FX mark material name");
        const auto name = AssetName(material->info.name, "mark material name");
        dependencies.Add("material", name);
        materials.push_back(name);
    }
    return materials;
}

inline Json Element(const IW3::FxElemDef &element, Dependencies &dependencies)
{
    const auto elemType = static_cast<unsigned char>(element.elemType);
    if (elemType >= IW3::FX_ELEM_TYPE_COUNT)
        throw std::runtime_error("Invalid IW3 FX element type");

    const auto visualCount = static_cast<unsigned char>(element.visualCount);
    if (visualCount > kMaxVisualCount)
        throw std::runtime_error("Invalid IW3 FX visual count");
    const auto velSampleCount = SampleCount(element.velIntervalCount, element.velSamples, "velocity");
    const auto visSampleCount = SampleCount(element.visStateIntervalCount, element.visSamples, "visual");

    Json velocitySamples = Json::array();
    for (size_t i = 0; i < velSampleCount; ++i)
        velocitySamples.push_back(VelocitySample(element.velSamples[i]));
    Json visualSamples = Json::array();
    for (size_t i = 0; i < visSampleCount; ++i)
        visualSamples.push_back(VisualSample(element.visSamples[i]));

    Json visuals = Json::array();
    bool visualsPresent = false;
    if (elemType == IW3::FX_ELEM_TYPE_DECAL)
    {
        visualsPresent = element.visuals.markArray != nullptr;
        if (visualCount && !element.visuals.markArray)
            throw std::runtime_error("Missing IW3 FX decal visuals");
        for (size_t i = 0; i < visualCount; ++i)
            visuals.push_back(MarkVisual(element.visuals.markArray[i], dependencies));
    }
    else if (visualCount > 1)
    {
        visualsPresent = element.visuals.array != nullptr;
        if (!element.visuals.array)
            throw std::runtime_error("Missing IW3 FX visual array");
        for (size_t i = 0; i < visualCount; ++i)
            visuals.push_back(Visual(element.visuals.array[i], elemType, dependencies));
    }
    else if (visualCount == 1)
    {
        visualsPresent = true;
        visuals.push_back(Visual(element.visuals.instance, elemType, dependencies));
    }

    Json result = {{"flags", element.flags},
                   {"spawn", Spawn(element.spawn)},
                   {"spawn_range", FloatRange(element.spawnRange)},
                   {"fade_in_range", FloatRange(element.fadeInRange)},
                   {"fade_out_range", FloatRange(element.fadeOutRange)},
                   {"spawn_frustum_cull_radius", element.spawnFrustumCullRadius},
                   {"spawn_delay_msec", IntRange(element.spawnDelayMsec)},
                   {"life_span_msec", IntRange(element.lifeSpanMsec)},
                   {"spawn_origin", Json::array()},
                   {"spawn_offset_radius", FloatRange(element.spawnOffsetRadius)},
                   {"spawn_offset_height", FloatRange(element.spawnOffsetHeight)},
                   {"spawn_angles", Json::array()},
                   {"angular_velocity", Json::array()},
                   {"initial_rotation", FloatRange(element.initialRotation)},
                   {"gravity", FloatRange(element.gravity)},
                   {"reflection_factor", FloatRange(element.reflectionFactor)},
                   {"atlas", Atlas(element.atlas)},
                   {"elem_type", elemType},
                   {"elem_type_name", VisualType(elemType)},
                   {"visual_count", visualCount},
                   {"vel_interval_count", static_cast<unsigned char>(element.velIntervalCount)},
                   {"vis_state_interval_count",
                    static_cast<unsigned char>(element.visStateIntervalCount)},
                   {"velocity_samples", std::move(velocitySamples)},
                   {"visual_samples", std::move(visualSamples)},
                   {"visuals_present", visualsPresent},
                   {"visuals", std::move(visuals)},
                   {"collision_mins", {element.collMins[0], element.collMins[1], element.collMins[2]}},
                   {"collision_maxs", {element.collMaxs[0], element.collMaxs[1], element.collMaxs[2]}},
                   {"effect_on_impact", EffectRef(element.effectOnImpact, dependencies)},
                   {"effect_on_death", EffectRef(element.effectOnDeath, dependencies)},
                   {"effect_emitted", EffectRef(element.effectEmitted, dependencies)},
                   {"emit_dist", FloatRange(element.emitDist)},
                   {"emit_dist_variance", FloatRange(element.emitDistVariance)},
                   {"sort_order", static_cast<unsigned char>(element.sortOrder)},
                   {"lighting_frac", static_cast<unsigned char>(element.lightingFrac)},
                   {"use_item_clip", static_cast<unsigned char>(element.useItemClip)}};

    for (const auto &range : element.spawnOrigin)
        result["spawn_origin"].push_back(FloatRange(range));
    for (const auto &range : element.spawnAngles)
        result["spawn_angles"].push_back(FloatRange(range));
    for (const auto &range : element.angularVelocity)
        result["angular_velocity"].push_back(FloatRange(range));
    result["trail"] = element.trailDef ? Trail(*element.trailDef) : Json(nullptr);
    return result;
}

inline size_t ElementCount(const IW3::FxEffectDef &effect)
{
    const auto looping = CheckedCount(effect.elemDefCountLooping, kMaxElementCount, "looping element");
    const auto oneShot = CheckedCount(effect.elemDefCountOneShot, kMaxElementCount, "one-shot element");
    const auto emission = CheckedCount(effect.elemDefCountEmission, kMaxElementCount, "emission element");
    if (looping > kMaxElementCount - oneShot || looping + oneShot > kMaxElementCount - emission)
        throw std::runtime_error("IW3 FX element count exceeds the limit");
    return looping + oneShot + emission;
}

inline Json Effect(const IW3::FxEffectDef &effect, Dependencies &dependencies,
                   const std::string &name)
{
    const auto count = ElementCount(effect);
    if (count && !effect.elemDefs)
        throw std::runtime_error("Missing IW3 FX element array");

    Json elements = Json::array();
    for (size_t i = 0; i < count; ++i)
        elements.push_back(Element(effect.elemDefs[i], dependencies));

    return {{"schema", 1},
            {"asset_type", "iw3_fx"},
            {"layout", {{"FxEffectDef", 32}, {"FxElemDef", 252}, {"FxTrailDef", 28}}},
            {"name", name},
            {"flags", effect.flags},
            {"total_size", effect.totalSize},
            {"msec_looping_life", effect.msecLoopingLife},
            {"element_counts",
             {{"looping", effect.elemDefCountLooping},
              {"one_shot", effect.elemDefCountOneShot},
              {"emission", effect.elemDefCountEmission},
              {"total", count}}},
            {"elements", std::move(elements)},
            {"dependencies", dependencies.Write()}};
}
} // namespace fx

class Fx final : public AbstractAssetDumper<IW3::AssetFx>
{
    bool ShouldDump(const XAssetInfo<IW3::FxEffectDef> &asset) override
    {
        // The filter is only for bounded validation. With no environment
        // override every source FX asset is exported.
        const auto *requested = std::getenv("REPLAY_EXPORT_IW3_FX_NAME");
        return !requested || !*requested || asset.m_name == requested;
    }

    void DumpAsset(AssetDumpingContext &context,
                   const XAssetInfo<IW3::FxEffectDef> &asset) override
    {
        const auto *effect = asset.Asset();
        if (!effect)
            throw std::runtime_error("Missing IW3 FX asset");
        const auto name = fx::ValidateAssetPath(effect->name);
        fx::Dependencies dependencies;
        const auto output = fx::Effect(*effect, dependencies, name);
        Save(context, std::string("fx/") + name + ".iw3.json", output);
    }
};

class ImpactFx final : public AbstractAssetDumper<IW3::AssetImpactFx>
{
    void DumpAsset(AssetDumpingContext &context,
                   const XAssetInfo<IW3::FxImpactTable> &asset) override
    {
        constexpr size_t kImpactCount = 12;
        constexpr size_t kNonFleshCount = 29;
        constexpr size_t kFleshCount = 4;

        const auto *table = asset.Asset();
        if (!table || !table->table)
            throw std::runtime_error("Missing IW3 impact FX table");

        const auto name = fx::BoundedString(table->name, "impact-table name");
        Json entries = Json::array();
        for (size_t row = 0; row < kImpactCount; ++row)
        {
            Json nonFlesh = Json::array();
            Json flesh = Json::array();
            for (size_t index = 0; index < kNonFleshCount; ++index)
            {
                const auto *effect = table->table[row].nonflesh[index];
                nonFlesh.push_back(effect && effect->name
                                       ? Json(fx::AssetName(effect->name, "impact FX name"))
                                       : Json(nullptr));
            }
            for (size_t index = 0; index < kFleshCount; ++index)
            {
                const auto *effect = table->table[row].flesh[index];
                flesh.push_back(effect && effect->name
                                    ? Json(fx::AssetName(effect->name, "impact FX name"))
                                    : Json(nullptr));
            }
            entries.push_back({{"index", row},
                               {"nonflesh", std::move(nonFlesh)},
                               {"flesh", std::move(flesh)}});
        }

        const auto fileName = name.empty() ? "_default" : fx::ValidateAssetPath(name.c_str());
        Save(context, std::string("impactfx/") + fileName + ".iw3.json",
             {{"schema", 1},
              {"asset_type", "iw3_impact_fx"},
              {"name", name},
              {"layout",
               {{"impact_count", kImpactCount},
                {"nonflesh_count", kNonFleshCount},
                {"flesh_count", kFleshCount}}},
              {"entries", std::move(entries)}});
    }
};

namespace sound
{
constexpr size_t kMaxAliasCount = 65536;

inline Json OptionalString(const char *value, const char *field)
{
    if (!value)
        return nullptr;
    return fx::BoundedString(value, field);
}

inline Json SpeakerMap(const IW3::SpeakerMap *map)
{
    if (!map)
        return nullptr;

    Json channels = Json::array();
    for (size_t sourceChannel = 0; sourceChannel < 2; ++sourceChannel)
        for (size_t destinationChannel = 0; destinationChannel < 2; ++destinationChannel)
        {
            const auto &channel = map->channelMaps[sourceChannel][destinationChannel];
            if (channel.speakerCount < 0 || channel.speakerCount > 6)
                throw std::runtime_error("Invalid IW3 sound speaker count");
            Json speakers = Json::array();
            for (int index = 0; index < channel.speakerCount; ++index)
            {
                const auto &speaker = channel.speakers[index];
                if (speaker.numLevels < 0 || speaker.numLevels > 2)
                    throw std::runtime_error("Invalid IW3 sound speaker level count");
                Json levels = Json::array();
                for (int level = 0; level < speaker.numLevels; ++level)
                    levels.push_back(speaker.levels[level]);
                speakers.push_back({{"speaker", speaker.speaker}, {"levels", std::move(levels)}});
            }
            channels.push_back({{"source", sourceChannel},
                                {"destination", destinationChannel},
                                {"speakers", std::move(speakers)}});
        }

    return {{"default", map->isDefault},
            {"name", OptionalString(map->name, "speaker-map name")},
            {"channels", std::move(channels)}};
}

inline Json File(const IW3::SoundFile *file)
{
    if (!file)
        return nullptr;

    Json result = {{"type", static_cast<unsigned>(static_cast<unsigned char>(file->type))},
                   {"exists", file->exists != 0}};
    if (file->type == IW3::SAT_LOADED)
    {
        result["kind"] = "loaded";
        result["name"] = file->u.loadSnd
                              ? OptionalString(file->u.loadSnd->name, "loaded-sound name")
                              : Json(nullptr);
    }
    else if (file->type == IW3::SAT_STREAMED)
    {
        result["kind"] = "streamed";
        result["directory"] = OptionalString(file->u.streamSnd.dir, "streamed-sound directory");
        result["name"] = OptionalString(file->u.streamSnd.name, "streamed-sound name");
    }
    else
    {
        result["kind"] = "unknown";
    }
    return result;
}

inline Json Alias(const IW3::snd_alias_t &alias)
{
    return {{"alias_name", OptionalString(alias.aliasName, "sound-alias name")},
            {"subtitle", OptionalString(alias.subtitle, "sound-alias subtitle")},
            {"secondary_alias",
             OptionalString(alias.secondaryAliasName, "secondary sound-alias name")},
            {"chain_alias", OptionalString(alias.chainAliasName, "chain sound-alias name")},
            {"file", File(alias.soundFile)},
            {"sequence", alias.sequence},
            {"volume", {{"min", alias.volMin}, {"max", alias.volMax}}},
            {"pitch", {{"min", alias.pitchMin}, {"max", alias.pitchMax}}},
            {"distance", {{"min", alias.distMin}, {"max", alias.distMax}}},
            {"flags", static_cast<uint32_t>(alias.flags)},
            {"slave_percentage", alias.slavePercentage},
            {"probability", alias.probability},
            {"lfe_percentage", alias.lfePercentage},
            {"center_percentage", alias.centerPercentage},
            {"start_delay", alias.startDelay},
            {"volume_falloff_curve",
             alias.volumeFalloffCurve
                 ? OptionalString(alias.volumeFalloffCurve->filename, "sound curve name")
                 : Json(nullptr)},
            {"envelope",
             {{"min", alias.envelopMin},
              {"max", alias.envelopMax},
              {"percentage", alias.envelopPercentage}}},
            {"speaker_map", SpeakerMap(alias.speakerMap)}};
}
} // namespace sound

class Sound final : public AbstractAssetDumper<IW3::AssetSound>
{
    void DumpAsset(AssetDumpingContext &context,
                   const XAssetInfo<IW3::snd_alias_list_t> &asset) override
    {
        const auto *list = asset.Asset();
        if (!list)
            throw std::runtime_error("Missing IW3 sound-alias list");
        if (list->count < 0 || static_cast<size_t>(list->count) > sound::kMaxAliasCount)
            throw std::runtime_error("Invalid IW3 sound-alias count");
        if (list->count && !list->head)
            throw std::runtime_error("Missing IW3 sound-alias records");

        const auto name = fx::ValidateAssetPath(list->aliasName);
        Json aliases = Json::array();
        for (int index = 0; index < list->count; ++index)
            aliases.push_back(sound::Alias(list->head[index]));
        Save(context, std::string("soundaliases/") + name + ".iw3.json",
             {{"schema", 1},
              {"asset_type", "iw3_sound_alias_list"},
              {"name", name},
              {"aliases", std::move(aliases)}});
    }
};

class Model final : public AbstractAssetDumper<IW3::AssetXModel>
{
    void DumpAsset(AssetDumpingContext &context, const XAssetInfo<IW3::XModel> &asset) override
    {
        const auto &model = *asset.Asset();
        if (!model.numLods || model.numLods > 4 || !model.surfs || !model.materialHandles)
            throw std::runtime_error("Invalid IW3 model surface array");
        Json lods = Json::array();
        for (unsigned lodIndex = 0; lodIndex < model.numLods; ++lodIndex)
        {
            const auto &lod = model.lodInfo[lodIndex];
            if (!lod.numsurfs || unsigned(lod.surfIndex) + lod.numsurfs > model.numsurfs)
                throw std::runtime_error("Invalid IW3 model LOD range");
            Json surfaces = Json::array();
            for (unsigned index = 0; index < lod.numsurfs; ++index)
            {
                const unsigned surfaceIndex = lod.surfIndex + index;
                const auto &surface = model.surfs[surfaceIndex];
                const auto *material = model.materialHandles[surfaceIndex];
                if (!surface.vertCount || !surface.triCount || !surface.verts0 ||
                    !surface.triIndices || !material || !material->info.name)
                    throw std::runtime_error("Invalid IW3 model geometry");
                Json vertices = Json::array(), indices = Json::array();
                for (unsigned vertexIndex = 0; vertexIndex < surface.vertCount; ++vertexIndex)
                {
                    const auto &vertex = surface.verts0[vertexIndex];
                    float uv[2], color[4];
                    IW3::Common::Vec2UnpackTexCoords(vertex.texCoord, uv);
                    IW3::Common::Vec4UnpackGfxColor(vertex.color, color);
                    vertices.push_back({{"position", V3(vertex.xyz.v)},
                                        {"normal", Normal(vertex.normal)},
                                        {"tangent", Normal(vertex.tangent)},
                                        {"binormal_sign", vertex.binormalSign},
                                        {"uv", {uv[0], uv[1]}},
                                        {"lightmap_uv", {0, 0}},
                                        {"color",
                                         {int(color[0] * 255 + .5f), int(color[1] * 255 + .5f),
                                          int(color[2] * 255 + .5f), int(color[3] * 255 + .5f)}}});
                }
                for (unsigned triangle = 0; triangle < surface.triCount; ++triangle)
                    for (const auto vertex : surface.triIndices[triangle].i)
                    {
                        if (vertex >= surface.vertCount)
                            throw std::runtime_error("Invalid IW3 model triangle index");
                        indices.push_back(vertex);
                    }
                const char *name = material->info.name;
                if (*name == ',')
                    ++name;
                surfaces.push_back({{"material", name},
                                    {"deformed", surface.deformed},
                                    {"reflection_probe", 0},
                                    {"vertices", std::move(vertices)},
                                    {"indices", std::move(indices)}});
            }
            lods.push_back({{"distance", lod.dist}, {"surfaces", std::move(surfaces)}});
        }
        Save(context, std::string("xmodel/") + model.name + ".replay.json",
             {{"schema", 1}, {"name", model.name}, {"lods", std::move(lods)}});
    }
};

class ComWorld final : public AbstractAssetDumper<IW3::AssetComWorld>
{
    void DumpAsset(AssetDumpingContext &context, const XAssetInfo<IW3::ComWorld> &asset) override
    {
        const auto &world = *asset.Asset();
        if (world.primaryLightCount > 65535 || (world.primaryLightCount && !world.primaryLights))
            throw std::runtime_error("Invalid IW3 primary-light array");

        Json lights = Json::array();
        for (unsigned int index = 0; index < world.primaryLightCount; ++index)
        {
            const auto &light = world.primaryLights[index];
            lights.push_back({{"type", static_cast<unsigned char>(light.type)},
                              {"can_use_shadow_map", light.canUseShadowMap != 0},
                              {"exponent", static_cast<unsigned char>(light.exponent)},
                              {"color", V3(light.color)},
                              {"direction", V3(light.dir)},
                              {"origin", V3(light.origin)},
                              {"radius", light.radius},
                              {"cos_half_fov_outer", light.cosHalfFovOuter},
                              {"cos_half_fov_inner", light.cosHalfFovInner},
                              {"cos_half_fov_expanded", light.cosHalfFovExpanded},
                              {"rotation_limit", light.rotationLimit},
                              {"translation_limit", light.translationLimit},
                              {"definition", light.defName ? light.defName : ""}});
        }

        Save(context, std::string(world.name) + ".replay-comworld.json",
             {{"schema", 1},
              {"name", world.name},
              {"in_use", world.isInUse != 0},
              {"primary_lights", std::move(lights)}});
    }
};

inline void LightGrid(AssetDumpingContext &context, const IW3::GfxWorld &world)
{
    const auto &grid = world.lightGrid;
    if (!grid.entryCount && !grid.colorCount)
    {
        Save(context, std::string(world.name) + ".lightgrid.json",
             {{"schema", 1}, {"name", world.name}, {"available", false}});
        return;
    }
    if (grid.rowAxis >= 3 || grid.colAxis >= 3 || grid.rowAxis == grid.colAxis ||
        grid.maxs[grid.rowAxis] < grid.mins[grid.rowAxis] ||
        grid.rawRowDataSize > 64u * 1024 * 1024 || grid.entryCount > 8u * 1024 * 1024 ||
        grid.colorCount > 65536)
        throw std::runtime_error("Invalid IW3 light-grid metadata");
    const size_t rows = grid.maxs[grid.rowAxis] - grid.mins[grid.rowAxis] + 1;
    const std::string stem = std::string(world.name) + ".lightgrid";
    auto binary = [&](const char *suffix, const void *data, size_t bytes) {
        if (bytes && !data)
            throw std::runtime_error("Missing IW3 light-grid array");
        const auto name = stem + suffix;
        auto file = context.OpenAssetFile(name);
        if (!file)
            throw std::runtime_error("Cannot write light-grid array");
        if (bytes)
            file->write(static_cast<const char *>(data), bytes);
        return Json{{"file", name}, {"bytes", bytes}};
    };
    Json out = {{"schema", 1},
                {"name", world.name},
                {"has_light_regions", grid.hasLightRegions},
                {"sun_primary_light_index", grid.sunPrimaryLightIndex},
                {"mins", {grid.mins[0], grid.mins[1], grid.mins[2]}},
                {"maxs", {grid.maxs[0], grid.maxs[1], grid.maxs[2]}},
                {"row_axis", grid.rowAxis},
                {"col_axis", grid.colAxis},
                {"row_count", rows},
                {"entry_count", grid.entryCount},
                {"color_count", grid.colorCount},
                {"row_starts", binary(".rows.bin", grid.rowDataStart, rows * sizeof(uint16_t))},
                {"row_data", binary(".raw.bin", grid.rawRowData, grid.rawRowDataSize)},
                {"entries", binary(".entries.bin", grid.entries,
                                   grid.entryCount * sizeof(IW3::GfxLightGridEntry))},
                {"colors", binary(".colors.bin", grid.colors,
                                  grid.colorCount * sizeof(IW3::GfxLightGridColors))}};
    Save(context, stem + ".json", out);
}

class World final : public AbstractAssetDumper<IW3::AssetGfxWorld>
{
    void DumpAsset(AssetDumpingContext &context, const XAssetInfo<IW3::GfxWorld> &asset) override
    try
    {
        const auto &w = *asset.Asset();
        LightGrid(context, w);
        std::cerr << "Replay world: " << w.name << " surfaces=" << w.surfaceCount
                  << " verts=" << w.vertexCount << " models=" << w.dpvs.smodelCount << std::endl;
        std::cerr << "pointers: surfaces=" << w.dpvs.surfaces << " vertices=" << w.vd.vertices
                  << " indices=" << w.indices << " models=" << w.models << std::endl;
        Json j = {{"schema", 1},
                  {"name", w.name},
                  {"bounds", {V3(w.mins), V3(w.maxs)}},
                  {"primary_light_count", w.primaryLightCount},
                  {"sun_primary_light_index", w.sunPrimaryLightIndex},
                  {"sky", w.skyImage ? w.skyImage->name : ""}};
        j["sun"] = {{"color", V3(w.sunColorFromBsp)},
                    {"angles", V3(w.sunParse.angles)},
                    {"intensity", w.sunParse.sunLight},
                    {"ambient", V3(w.sunParse.ambientColor)}};
        j["lightmaps"] = Json::array();
        j["reflection_probes"] = Json::array();

        if (!w.reflectionProbeCount || w.reflectionProbeCount > 256 || !w.reflectionProbes)
            throw std::runtime_error("Invalid IW3 reflection-probe table");
        for (unsigned index = 0; index < w.reflectionProbeCount; ++index)
        {
            const auto &probe = w.reflectionProbes[index];
            if (!probe.reflectionImage || !probe.reflectionImage->name)
                throw std::runtime_error("Missing IW3 reflection-probe image");
            j["reflection_probes"].push_back(
                {{"origin", V3(probe.origin)}, {"image", probe.reflectionImage->name}});
        }

        if (!w.dpvsPlanes.cellCount || w.dpvsPlanes.cellCount > 65535 || w.planeCount < 0 ||
            w.planeCount > 65535 || w.nodeCount <= 0 || w.nodeCount > 65535 ||
            !w.dpvsPlanes.planes || !w.dpvsPlanes.nodes || !w.cells)
            throw std::runtime_error("Invalid IW3 DPVS topology");
        j["dpvs"] = {{"planes", Json::array()}, {"nodes", Json::array()}, {"cells", Json::array()}};
        for (int i = 0; i < w.planeCount; ++i)
        {
            const auto &plane = w.dpvsPlanes.planes[i];
            j["dpvs"]["planes"].push_back({{"normal", V3(plane.normal)},
                                           {"dist", plane.dist},
                                           {"type", static_cast<unsigned char>(plane.type)}});
        }
        for (int i = 0; i < w.nodeCount; ++i)
            j["dpvs"]["nodes"].push_back(w.dpvsPlanes.nodes[i]);
        for (unsigned i = 0; i < static_cast<unsigned>(w.dpvsPlanes.cellCount); ++i)
        {
            const auto &cell = w.cells[i];
            if (cell.aabbTreeCount < 0 || cell.portalCount < 0 ||
                (cell.aabbTreeCount && !cell.aabbTree) || (cell.portalCount && !cell.portals))
                throw std::runtime_error("Invalid IW3 DPVS cell");
            Json out = {{"bounds", {V3(cell.mins), V3(cell.maxs)}},
                        {"surfaces", Json::array()},
                        {"models", Json::array()},
                        {"trees", Json::array()},
                        {"portals", Json::array()},
                        {"reflection_probes", Json::array()}};
            const unsigned reflectionProbeCount =
                static_cast<unsigned char>(cell.reflectionProbeCount);
            if (reflectionProbeCount && !cell.reflectionProbes)
                throw std::runtime_error("Missing IW3 cell reflection-probe indices");
            for (unsigned probe = 0; probe < reflectionProbeCount; ++probe)
            {
                const unsigned index = static_cast<unsigned char>(cell.reflectionProbes[probe]);
                if (index >= w.reflectionProbeCount)
                    throw std::runtime_error("Invalid IW3 cell reflection-probe index");
                out["reflection_probes"].push_back(index);
            }
            for (int treeIndex = 0; treeIndex < cell.aabbTreeCount; ++treeIndex)
            {
                const auto &tree = cell.aabbTree[treeIndex];
                if (static_cast<unsigned>(tree.startSurfIndex) + tree.surfaceCount >
                        static_cast<unsigned>(w.surfaceCount) ||
                    (tree.smodelIndexCount && !tree.smodelIndexes))
                    throw std::runtime_error("Invalid IW3 cell AABB tree");
                int firstChild = 0;
                if (tree.childCount)
                {
                    const int treeSize = static_cast<int>(sizeof(IW3::GfxAabbTree));
                    if (tree.childrenOffset <= 0 || tree.childrenOffset % treeSize != 0)
                        throw std::runtime_error("Invalid IW3 cell AABB child offset");
                    firstChild = treeIndex + tree.childrenOffset / treeSize;
                    if (firstChild < 0 || firstChild + tree.childCount > cell.aabbTreeCount)
                        throw std::runtime_error("Invalid IW3 cell AABB child range");
                }
                Json outTree = {{"bounds", {V3(tree.mins), V3(tree.maxs)}},
                                {"surfaces", Json::array()},
                                {"models", Json::array()},
                                {"childCount", tree.childCount},
                                {"firstChild", firstChild}};
                for (unsigned surface = 0; surface < tree.surfaceCount; ++surface)
                    outTree["surfaces"].push_back(tree.startSurfIndex + surface);
                for (unsigned model = 0; model < tree.smodelIndexCount; ++model)
                {
                    if (tree.smodelIndexes[model] >= w.dpvs.smodelCount)
                        throw std::runtime_error("Invalid IW3 cell static-model index");
                    outTree["models"].push_back(tree.smodelIndexes[model]);
                }
                out["surfaces"].insert(out["surfaces"].end(), outTree["surfaces"].begin(),
                                       outTree["surfaces"].end());
                out["models"].insert(out["models"].end(), outTree["models"].begin(),
                                     outTree["models"].end());
                out["trees"].push_back(std::move(outTree));
            }
            for (int portalIndex = 0; portalIndex < cell.portalCount; ++portalIndex)
            {
                const auto &portal = cell.portals[portalIndex];
                const auto adjacent = portal.cell - w.cells;
                const unsigned vertexCount = static_cast<unsigned char>(portal.vertexCount);
                if (adjacent < 0 || adjacent >= w.dpvsPlanes.cellCount ||
                    (vertexCount && !portal.vertices))
                    throw std::runtime_error("Invalid IW3 DPVS portal");
                Json vertices = Json::array();
                for (unsigned vertex = 0; vertex < vertexCount; ++vertex)
                    vertices.push_back(V3(portal.vertices[vertex].v));
                out["portals"].push_back(
                    {{"plane",
                      {portal.plane.coeffs[0], portal.plane.coeffs[1], portal.plane.coeffs[2],
                       portal.plane.coeffs[3]}},
                     {"cell", adjacent},
                     {"vertices", std::move(vertices)},
                     {"hull_axis", {V3(portal.hullAxis[0]), V3(portal.hullAxis[1])}}});
            }
            auto unique = [](Json &values) {
                auto items = values.get<std::vector<unsigned>>();
                std::sort(items.begin(), items.end());
                items.erase(std::unique(items.begin(), items.end()), items.end());
                values = std::move(items);
            };
            unique(out["surfaces"]);
            unique(out["models"]);
            j["dpvs"]["cells"].push_back(std::move(out));
        }
        for (int i = 0; i < w.lightmapCount; ++i)
        {
            Json pair = Json::array();
            for (const auto *image : {w.lightmaps[i].primary, w.lightmaps[i].secondary})
            {
                if (!image || !image->texture.loadDef)
                    throw std::runtime_error("Missing compiled lightmap pixels");
                const auto &pixels = *image->texture.loadDef;
                const auto file = std::string(w.name) + ".lightmap_" + std::to_string(i) + "_" +
                                  std::to_string(pair.size()) + ".bin";
                auto output = context.OpenAssetFile(file);
                if (!output)
                    throw std::runtime_error("Cannot write lightmap");
                output->write(pixels.data, pixels.resourceSize);
                pair.push_back({{"file", file},
                                {"width", image->width},
                                {"height", image->height},
                                {"format", pixels.format},
                                {"bytes", pixels.resourceSize},
                                {"levels", pixels.levelCount}});
            }
            j["lightmaps"].push_back(pair);
        }
        j["surfaces"] = Json::array();
        j["models"] = Json::array();
        j["brush_models"] = Json::array();
        for (int i = 0; i < w.modelCount; ++i)
        {
            const auto &m = w.models[i];
            j["brush_models"].push_back({{"start", m.startSurfIndex},
                                         {"count", m.surfaceCount},
                                         {"bounds", {V3(m.bounds[0]), V3(m.bounds[1])}}});
        }
        for (int i = 0; i < w.surfaceCount; ++i)
        {
            const auto &s = w.dpvs.surfaces[i];
            if (i < 3)
                std::cerr << "surface " << i << " mat=" << s.material
                          << " first=" << s.tris.firstVertex << " count=" << s.tris.vertexCount
                          << std::endl;
            if (!s.material)
                throw std::runtime_error("Missing surface material");
            if (i == 0)
            {
                for (unsigned ti = 0; ti < 34; ++ti)
                {
                    auto *t = s.material->techniqueSet->techniques[ti];
                    if (!t)
                        continue;
                    for (unsigned pi = 0; pi < t->passCount; ++pi)
                    {
                        auto *ps = t->passArray[pi].pixelShader;
                        if (!ps)
                            continue;
                        auto file = context.OpenAssetFile("replay_shaders/" +
                                                          std::string(ps->name) + ".bin");
                        file->write(reinterpret_cast<const char *>(ps->prog.loadDef.program),
                                    ps->prog.loadDef.programSize * 4);
                    }
                }
            }
            Json out = {{"material", s.material->info.name},
                        {"lightmap", static_cast<unsigned char>(s.lightmapIndex)},
                        {"reflection_probe", static_cast<unsigned char>(s.reflectionProbeIndex)},
                        {"vertices", Json::array()},
                        {"indices", Json::array()}};
            if (static_cast<unsigned char>(s.reflectionProbeIndex) >= w.reflectionProbeCount)
                throw std::runtime_error("Invalid IW3 surface reflection-probe index");
            if (s.tris.firstVertex < 0 ||
                static_cast<unsigned>(s.tris.firstVertex) + s.tris.vertexCount > w.vertexCount ||
                s.tris.baseIndex < 0 || s.tris.baseIndex + 3 * s.tris.triCount > w.indexCount)
                throw std::runtime_error("Invalid IW3 surface range");
            std::unordered_map<unsigned, unsigned> remap;
            for (unsigned k = 0; k < 3u * s.tris.triCount; ++k)
            {
                const auto index = w.indices[s.tris.baseIndex + k];
                if (index >= s.tris.vertexCount)
                    throw std::runtime_error("Invalid IW3 local surface index: surface=" +
                                             std::to_string(i) + " index=" + std::to_string(index) +
                                             " first=" + std::to_string(s.tris.firstVertex) +
                                             " count=" + std::to_string(s.tris.vertexCount));
                const auto [it, inserted] =
                    remap.emplace(index, static_cast<unsigned>(remap.size()));
                if (inserted)
                {
                    const auto &v = w.vd.vertices[s.tris.firstVertex + index];
                    out["vertices"].push_back({{"position", V3(v.xyz)},
                                               {"uv", {v.texCoord[0], v.texCoord[1]}},
                                               {"normal", Normal(v.normal)},
                                               {"tangent", Normal(v.tangent)},
                                               {"binormal_sign", v.binormalSign},
                                               {"lightmap_uv", {v.lmapCoord[0], v.lmapCoord[1]}},
                                               {"color",
                                                {static_cast<unsigned char>(v.color.array[0]),
                                                 static_cast<unsigned char>(v.color.array[1]),
                                                 static_cast<unsigned char>(v.color.array[2]),
                                                 static_cast<unsigned char>(v.color.array[3])}}});
                }
                out["indices"].push_back(it->second);
            }
            j["surfaces"].push_back(std::move(out));
            if (i % 512 == 0)
                std::cerr << "exported surface " << i << std::endl;
        }
        for (unsigned i = 0; i < w.dpvs.smodelCount; ++i)
        {
            const auto &m = w.dpvs.smodelDrawInsts[i];
            const auto &p = m.placement;
            if (static_cast<unsigned char>(m.reflectionProbeIndex) >= w.reflectionProbeCount)
                throw std::runtime_error("Invalid IW3 static-model reflection-probe index");
            j["models"].push_back(
                {{"model", m.model->name},
                 {"origin", V3(p.origin)},
                 {"axis", {V3(p.axis[0]), V3(p.axis[1]), V3(p.axis[2])}},
                 {"scale", p.scale},
                 {"reflection_probe", static_cast<unsigned char>(m.reflectionProbeIndex)},
                 {"ground_lighting", w.dpvs.smodelInsts[i].groundLighting.packed}});
        }
        Save(context, std::string(w.name) + ".replay-world.json", j);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Replay export failed: " << e.what() << std::endl;
        throw;
    }
};

template <class AssetType> class Collision final : public AbstractAssetDumper<AssetType>
{
    void DumpAsset(AssetDumpingContext &context, const XAssetInfo<IW3::clipMap_t> &asset) override
    {
        const auto &c = *asset.Asset();
        Json j = {{"schema", 1},
                  {"name", c.name},
                  {"brushes", Json::array()},
                  {"vertices", Json::array()},
                  {"triangles", Json::array()},
                  {"static_models", Json::array()},
                  {"submodel_count", c.numSubModels},
                  {"dynamic_model_count", c.dynEntCount[0]},
                  {"dynamic_brush_count", c.dynEntCount[1]},
                  {"dynamic_models", Json::array()},
                  {"dynamic_brushes", Json::array()},
                  {"submodels", Json::array()},
                  {"leaf_brush_nodes", Json::array()}};
        for (unsigned basis = 0; basis < 2; ++basis)
        {
            auto &output = basis == 0 ? j["dynamic_models"] : j["dynamic_brushes"];
            if (c.dynEntCount[basis] && !c.dynEntDefList[basis])
                throw std::runtime_error("Missing IW3 dynamic-entity definitions");
            for (unsigned i = 0; i < c.dynEntCount[basis]; ++i)
            {
                const auto &entity = c.dynEntDefList[basis][i];
                output.push_back(
                    {{"type", static_cast<unsigned>(entity.type)},
                     {"quaternion",
                      {entity.pose.quat[0], entity.pose.quat[1], entity.pose.quat[2],
                       entity.pose.quat[3]}},
                     {"origin", V3(entity.pose.origin)},
                     {"model", entity.xModel && entity.xModel->name ? entity.xModel->name : ""},
                     {"brush_model", entity.brushModel},
                     {"physics_brush_model", entity.physicsBrushModel},
                     {"destroy_fx",
                      entity.destroyFx && entity.destroyFx->name ? entity.destroyFx->name : ""},
                     {"destroy_pieces", entity.destroyPieces && entity.destroyPieces->name
                                            ? entity.destroyPieces->name
                                            : ""},
                     {"physics_preset",
                      entity.physPreset && entity.physPreset->name ? entity.physPreset->name : ""},
                     {"health", entity.health},
                     {"center_of_mass", V3(entity.mass.centerOfMass)},
                     {"moments_of_inertia", V3(entity.mass.momentsOfInertia)},
                     {"products_of_inertia", V3(entity.mass.productsOfInertia)},
                     {"contents", entity.contents}});
            }
        }
        for (unsigned i = 0; i < c.numSubModels; ++i)
        {
            const auto &m = c.cmodels[i];
            j["submodels"].push_back(
                {{"mins", V3(m.mins)}, {"maxs", V3(m.maxs)}, {"leaf", m.leaf.leafBrushNode}});
        }
        for (unsigned i = 0; i < c.leafbrushNodesCount; ++i)
        {
            const auto &n = c.leafbrushNodes[i];
            Json node = {{"count", n.leafBrushCount}, {"brushes", Json::array()}};
            if (n.leafBrushCount > 0)
                for (int k = 0; k < n.leafBrushCount; ++k)
                    node["brushes"].push_back(n.data.leaf.brushes[k]);
            else
                node["children"] = {n.data.children.childOffset[0], n.data.children.childOffset[1]};
            j["leaf_brush_nodes"].push_back(std::move(node));
        }
        for (unsigned i = 0; i < c.numBrushes; ++i)
        {
            const auto &b = c.brushes[i];
            Json out = {{"mins", V3(b.mins)},
                        {"maxs", V3(b.maxs)},
                        {"contents", b.contents},
                        {"planes", Json::array()},
                        {"ladder_planes", Json::array()}};
            auto ladder = [&](unsigned material, const float *n, float dist) {
                if (material < c.numMaterials && (c.materials[material].surfaceFlags & 8))
                    out["ladder_planes"].push_back({n[0], n[1], n[2], dist});
            };
            for (int side = 0; side < 2; ++side)
                for (int axis = 0; axis < 3; ++axis)
                {
                    float n[3]{};
                    n[axis] = side ? 1.f : -1.f;
                    ladder(b.axialMaterialNum[side][axis], n, side ? b.maxs[axis] : -b.mins[axis]);
                }
            for (unsigned k = 0; k < b.numsides; ++k)
            {
                const auto &p = *b.sides[k].plane;
                out["planes"].push_back({p.normal[0], p.normal[1], p.normal[2], p.dist});
                ladder(b.sides[k].materialNum, p.normal, p.dist);
            }
            j["brushes"].push_back(std::move(out));
        }
        for (unsigned i = 0; i < c.vertCount; ++i)
            j["vertices"].push_back(V3(c.verts[i].v));
        for (int i = 0; i < c.triCount; ++i)
            j["triangles"].push_back(
                {c.triIndices[i * 3], c.triIndices[i * 3 + 1], c.triIndices[i * 3 + 2]});
        std::vector<uint32_t> triangleContents(c.triCount);
        std::vector<bool> mappedTriangles(c.triCount);
        for (int i = 0; i < c.aabbTreeCount; ++i)
        {
            const auto &tree = c.aabbTrees[i];
            if (tree.childCount)
                continue;
            if (tree.materialIndex >= c.numMaterials || tree.u.partitionIndex < 0 ||
                tree.u.partitionIndex >= c.partitionCount)
                throw std::runtime_error("Invalid collision tree material or partition");
            const auto &partition = c.partitions[tree.u.partitionIndex];
            if (partition.firstTri < 0 || partition.firstTri > c.triCount ||
                partition.triCount > c.triCount - partition.firstTri)
                throw std::runtime_error("Invalid collision partition triangle span");
            const auto contents = uint32_t(c.materials[tree.materialIndex].contentFlags);
            for (int t = partition.firstTri; t < partition.firstTri + partition.triCount; ++t)
            {
                triangleContents[t] |= contents;
                mappedTriangles[t] = true;
            }
        }
        j["triangle_contents_schema"] = "material-partitions-v1";
        j["triangle_contents"] = Json::array();
        for (int i = 0; i < c.triCount; ++i)
            j["triangle_contents"].push_back(mappedTriangles[i] ? Json(triangleContents[i])
                                                                : Json(nullptr));
        for (unsigned i = 0; i < c.numStaticModels; ++i)
        {
            const auto &m = c.staticModelList[i];
            j["static_models"].push_back({{"model", m.xmodel->name}, {"origin", V3(m.origin)}});
        }
        Save(context, std::string(c.name) + ".replay-collision.json", j);
    }
};
} // namespace replay_export
