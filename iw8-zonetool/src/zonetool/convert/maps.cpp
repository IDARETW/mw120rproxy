#include "../iw8/replay_havok.h"
#include "maps_convert.h"
#include "common/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace convert
{
bool iw3KeyToIw8KeyId(const std::string &key, uint32_t &keyId)
{
    if (iw8::havok::FindOpaqueString(key, keyId))
        return true;

    // Replay 1.20's opaque-string table. These are the IW3 entity keys used by
    // the native map classes that the converter currently carries forward.
    static constexpr std::array<std::pair<const char *, uint32_t>, 27> ids{{
        {"ambient", 0x0048},
        {"angles", 0x0050},
        {"classname", 0x00D4},
        {"destructible_type", 0x013A},
        {"diffusefraction", 0x013D},
        {"height", 0x01E9},
        {"model", 0x028C},
        {"origin", 0x02C5},
        {"radius", 0x030E},
        {"script_exploder", 0x0349},
        {"script_linkname", 0x034A},
        {"script_linkto", 0x034B},
        {"script_noteworthy", 0x034D},
        {"spawnflags", 0x039D},
        {"suncolor", 0x03C7},
        {"sundirection", 0x03C8},
        {"sunlight", 0x03C9},
        {"target", 0x042E},
        {"targetname", 0x0430},
        {"_color", 0x09F9},
        {"script_accel", 0xAA69},
        {"script_airspeed", 0xAA70},
        {"script_bombmode_original", 0xAAA4},
        {"script_fxid", 0xAB3D},
        {"script_gameobjectname", 0xAB40},
        {"script_label", 0xAB6F},
        {"physasset", 0x9333},
    }};
    const auto found =
        std::find_if(ids.begin(), ids.end(), [&](const auto &entry) { return key == entry.first; });
    if (found != ids.end())
    {
        keyId = found->second;
        return true;
    }
    return false;
}

namespace
{
struct KeyValue
{
    std::string key;
    std::string value;
};
using Entity = std::vector<KeyValue>;

constexpr const char *ReplayBaselineAnchor =
    "{ 212 \"script_model\" 709 \"0 0 16\" 80 \"0 0 0\" }\n";

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<Entity> parseEntities(const std::string &text)
{
    enum class State
    {
        awaitKey,
        readKey,
        awaitValue,
        readValue,
    };

    State state = State::awaitKey;
    std::string key;
    std::string value;
    Entity current;
    std::vector<Entity> entities;

    for (const char character : text)
    {
        switch (character)
        {
        case '{':
            current.clear();
            state = State::awaitKey;
            break;
        case '}':
            entities.push_back(current);
            current.clear();
            state = State::awaitKey;
            break;
        case '"':
            if (state == State::awaitKey)
            {
                key.clear();
                state = State::readKey;
            }
            else if (state == State::readKey)
            {
                state = State::awaitValue;
            }
            else if (state == State::awaitValue)
            {
                value.clear();
                state = State::readValue;
            }
            else
            {
                current.push_back({key, value});
                state = State::awaitKey;
            }
            break;
        default:
            if (state == State::readKey)
            {
                key.push_back(character);
            }
            else if (state == State::readValue)
            {
                value.push_back(character);
            }
            break;
        }
    }
    return entities;
}

std::map<std::string, size_t> iw8ClassCounts(const std::string &text)
{
    const std::regex entity(R"(\{([^{}]*)\})");
    const std::regex field(R"field((\d+)\s+"([^"]*)")field");
    std::map<std::string, size_t> counts;
    for (auto item = std::sregex_iterator(text.begin(), text.end(), entity);
         item != std::sregex_iterator(); ++item)
    {
        const std::string body = (*item)[1].str();
        for (auto pair = std::sregex_iterator(body.begin(), body.end(), field);
             pair != std::sregex_iterator(); ++pair)
        {
            if ((*pair)[1].str() == "212")
            {
                ++counts[lowercase((*pair)[2].str())];
                break;
            }
        }
    }
    return counts;
}

bool isReplayEntity(const std::string &classname)
{
    // Keep this list closed.  An arbitrary `mp_*_spawn` or `trigger_*` class can
    // carry gameplay state that Replay does not understand; accepting it as a
    // generic entity would make a map appear converted while losing that state.
    static constexpr std::array<const char *, 23> classes{{
        "worldspawn",
        "info_player_start",
        "mp_global_intermission",
        "mp_ctf_spawn_allies_start",
        "mp_ctf_spawn_axis_start",
        "mp_dom_spawn_allies_start",
        "mp_dom_spawn_axis_start",
        "mp_sab_spawn_allies_start",
        "mp_sab_spawn_axis_start",
        "mp_sd_spawn_attacker",
        "mp_sd_spawn_defender",
        "mp_tdm_spawn",
        "mp_tdm_spawn_allies_start",
        "mp_tdm_spawn_axis_start",
        "script_brushmodel",
        "script_model",
        "script_origin",
        "trigger_damage",
        "trigger_hurt",
        "trigger_multiple",
        "trigger_radius",
        "trigger_use",
        "trigger_use_touch",
    }};
    return std::find(classes.begin(), classes.end(), classname) != classes.end();
}

bool isModeSpecificSpawn(const std::string &classname)
{
    // Replay's current custom-map contract consumes the TDM spawn set. Drop
    // only these verified alternate-mode markers until their native game-mode
    // consumers are part of the package contract.
    static constexpr std::array<const char *, 6> classes{{
        "mp_ctf_spawn_allies",
        "mp_ctf_spawn_axis",
        "mp_dm_spawn",
        "mp_dom_spawn",
        "mp_sab_spawn_allies",
        "mp_sab_spawn_axis",
    }};
    for (const char *known : classes)
    {
        if (classname == known)
            return true;
    }
    return false;
}

bool isSourceOnlyMarker(const std::string &classname)
{
    // IW3 emits these records for FX placement and AI traversal authoring. The
    // current Replay map contract has no native fx_origin or aipaths asset
    // writer, so retaining the markers would advertise state with no consumer.
    static constexpr std::array<const char *, 4> classes{{
        "fx_origin",
        "node_negotiation_begin",
        "node_negotiation_end",
        "node_pathnode",
    }};
    return std::find(classes.begin(), classes.end(), classname) != classes.end();
}

bool isBarrelFireFxStruct(const Entity &entity)
{
    const auto target = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
        return lowercase(pair.key) == "targetname";
    });
    if (target == entity.end() || target->value != "barrel_fireFX_origin")
        return false;
    return std::all_of(entity.begin(), entity.end(), [](const KeyValue &pair) {
        const auto key = lowercase(pair.key);
        return key == "classname" || key == "targetname" || key == "origin" ||
               key == "angles";
    });
}

bool isOldSchoolPickup(const std::string &classname)
{
    // These entities are consumed only by IW3's old-school ruleset. Replay's
    // standard custom-map game mode has no equivalent native pickup contract,
    // and retaining the IW3 weapon classname would make the server instantiate
    // an unsupported gameplay entity. Keep the projection explicit and noisy.
    static constexpr std::array<const char *, 16> classes{{
        "weapon_ak47_mp",
        "weapon_dragunov_mp",
        "weapon_frag_grenade_mp",
        "weapon_g36c_silencer_mp",
        "weapon_g3_mp",
        "weapon_g3_reflex_mp",
        "weapon_m1014_grip_mp",
        "weapon_m14_reflex_mp",
        "weapon_m4_gl_mp",
        "weapon_m60e4_acog_mp",
        "weapon_m40a3_mp",
        "weapon_mp5_silencer_mp",
        "weapon_p90_mp",
        "weapon_p90_silencer_mp",
        "weapon_rpg_mp",
        "weapon_winchester1200_grip_mp",
    }};
    return std::find(classes.begin(), classes.end(), classname) != classes.end();
}

const KeyValue *entityModel(const Entity &entity)
{
    const auto model = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
        return lowercase(pair.key) == "model";
    });
    return model == entity.end() ? nullptr : &*model;
}

uint32_t brushModelIndex(const Entity &entity)
{
    const auto model = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
        return lowercase(pair.key) == "model";
    });
    if (model == entity.end() || model->value.size() < 2 || model->value.front() != '*')
        throw std::runtime_error("IW3 brush trigger has no *n collision model");

    uint32_t index = 0;
    const char *first = model->value.data() + 1;
    const char *last = model->value.data() + model->value.size();
    const auto parsed = std::from_chars(first, last, index);
    if (parsed.ec != std::errc{} || parsed.ptr != last || index == 0)
        throw std::runtime_error("IW3 brush trigger has an invalid collision model");
    return index;
}
} // namespace

std::vector<std::string> iw3ReplayEntityModels(const std::string &input)
{
    std::vector<std::string> models;
    for (const Entity &entity : parseEntities(input))
    {
        const auto classname = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
            return lowercase(pair.key) == "classname";
        });
        if (classname == entity.end())
            continue;
        const std::string name = lowercase(classname->value);
        // `misc_model` is already represented by world.models and the native
        // GfxWorldStaticModels table.  Re-emitting it as an entity XModel would
        // draw the same placement twice (mp_test has an exact two-instance
        // match).  A `trigger_radius` model is a real point-entity asset and
        // must be resolved separately from brush trigger `*n` models.
        if (name != "script_model" && name != "trigger_radius")
            continue;

        const KeyValue *model = entityModel(entity);
        if (model == nullptr || model->value.empty() || model->value.front() == '*')
            continue;
        if (std::find(models.begin(), models.end(), model->value) == models.end())
            models.push_back(model->value);
    }
    return models;
}

void validateIw8EntityString(const std::string &input)
{
    const auto counts = iw8ClassCounts(input);
    const auto require = [&](const char *classname) {
        const auto item = counts.find(classname);
        if (item == counts.end() || item->second == 0)
        {
            throw std::runtime_error(std::string("Missing required Replay entity: ") + classname);
        }
    };
    require("worldspawn");
    require("mp_tdm_spawn");
    require("mp_tdm_spawn_allies_start");
    require("mp_tdm_spawn_axis_start");
    require("script_model");
}

size_t iw3ToIw8EntityString(const std::string &input, std::string &output,
                            std::vector<TriggerModelSource> *triggers)
{
    output.clear();
    if (triggers)
        triggers->clear();
    size_t emitted = 0;
    size_t projectedOldSchoolPickups = 0;
    std::map<std::string, size_t> projectedSourceMarkers;
    std::map<std::string, std::pair<size_t, std::string>> unsupported;

    for (const Entity &entity : parseEntities(input))
    {
        const auto classname = std::find_if(entity.begin(), entity.end(), [](const KeyValue &pair) {
            return lowercase(pair.key) == "classname";
        });
        if (classname == entity.end())
        {
            continue;
        }

        std::string name = lowercase(classname->value);
        if (name == "misc_model")
        {
            // IW3 static-model instances are loaded from world.models and
            // emitted into GfxWorldStaticModels by the native map path.  Keep
            // them out of MapEnts so render and collision are not duplicated.
            continue;
        }
        if (isSourceOnlyMarker(name) || isModeSpecificSpawn(name))
        {
            ++projectedSourceMarkers[name];
            continue;
        }
        if (name == "script_struct" && isBarrelFireFxStruct(entity))
        {
            // IW3's _global_fx.gsc owns these barrel-fire placement markers.
            // Until source FX graphs have native Replay emitters, retain the
            // precise omission in the source-marker audit.
            ++projectedSourceMarkers["script_struct:barrel_fireFX_origin"];
            continue;
        }
        if (!isReplayEntity(name))
        {
            if (isOldSchoolPickup(name))
            {
                ++projectedOldSchoolPickups;
            }
            else
            {
                const KeyValue *model = entityModel(entity);
                auto &entry = unsupported[name];
                ++entry.first;
                if (entry.second.empty() && model != nullptr && !model->value.empty())
                    entry.second = model->value;
            }
            continue;
        }

        const KeyValue *model = entityModel(entity);
        const bool trigger = name.starts_with("trigger_") && model != nullptr &&
                             !model->value.empty() && model->value.front() == '*';
        uint32_t triggerIndex = 0;
        if (name == "trigger_damage")
            name = "trigger_hurt";
        if (trigger)
        {
            if (!triggers)
                throw std::runtime_error("IW3 trigger conversion requires native trigger output");
            triggerIndex = static_cast<uint32_t>(triggers->size());
            triggers->push_back({brushModelIndex(entity), true});
        }

        std::string body;
        for (const KeyValue &pair : entity)
        {
            uint32_t keyId = 0;
            if (!iw3KeyToIw8KeyId(lowercase(pair.key), keyId))
            {
                continue;
            }
            if (!body.empty())
            {
                body.push_back(' ');
            }
            body += std::to_string(keyId);
            body += " \"";
            if (keyId == 0x00D4)
                body += name;
            else if (trigger && keyId == 0x028C)
                body += "?" + std::to_string(triggerIndex);
            else
                body += pair.value;
            body += "\"";
        }

        if (trigger)
        {
            if (!body.empty())
                body.push_back(' ');
            body += "37683 \"TriggerModelStaticDummyDefault\"";
        }

        if (body.empty())
        {
            continue;
        }
        output += "{ " + body + " }\n";
        ++emitted;
    }

    if (!unsupported.empty())
    {
        std::string message =
            "IW3 entity conversion rejected unsupported entity class(es): ";
        bool first = true;
        for (const auto &[classname, details] : unsupported)
        {
            if (!first)
                message += ", ";
            first = false;
            message += classname + " (" + std::to_string(details.first) + " instance";
            if (details.first != 1)
                message += "s";
            if (!details.second.empty())
                message += ", model '" + details.second + "'";
            message += ")";
        }
        message +=
            ". Add a native entity mapping before converting this map; verified mode-specific "
            "spawn markers are intentionally projected to Replay's TDM spawn set.";
        throw std::runtime_error(message);
    }

    if (projectedOldSchoolPickups)
        zt::warn("iw3: projected out %zu old-school weapon pickup entities; Replay's standard "
                 "custom-map mode has no native IW3 pickup contract",
                 projectedOldSchoolPickups);

    if (!projectedSourceMarkers.empty())
    {
        size_t total = 0;
        for (const auto &[classname, count] : projectedSourceMarkers)
        {
            total += count;
            zt::warn("iw3: projected out %zu '%s' source-only/editor entity record(s); "
                     "no native Replay consumer is emitted",
                     count, classname.c_str());
        }
        zt::warn("iw3: projected out %zu source-only/editor/mode entity record(s); "
                 "native gameplay state requires a corresponding Replay asset contract",
                 total);
    }

    // Baked world props and spawn markers do not enter Replay's non-player
    // snapshot baseline. Keep one harmless runtime entity so the client can
    // complete the baseline before the server switches to delta snapshots.
    output += ReplayBaselineAnchor;
    ++emitted;
    validateIw8EntityString(output);
    return emitted;
}
} // namespace convert
