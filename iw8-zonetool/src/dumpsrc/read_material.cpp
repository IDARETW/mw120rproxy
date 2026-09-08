

#include "dumpsrc/material_dumpsrc.h"
#include "common/fs_util.h"
#include "common/log.h"
#include "common/json.hpp"
#include <exception>

using json = nlohmann::json;
using namespace zt;

namespace convdump::mtl {

// Read a JSON value as a SIGNED int regardless of whether it was stored as int/float/null. The IW5 dump
// writes sampleState as a signed char (it CAN be negative, e.g. -21 on sky_chechnya), and the other
// scalars as ints; json.hpp would throw on a get<int>() of a null, so guard nulls -> default.
static int j_int(const json& parent, const char* key, int def) {
    auto it = parent.find(key);
    if (it == parent.end() || it->is_null())
        return def;
    if (it->is_number_integer() || it->is_number_unsigned())
        return it->get<int>();
    if (it->is_number_float())
        return static_cast<int>(it->get<double>());
    if (it->is_boolean())
        return it->get<bool>() ? 1 : 0;
    return def;
}
static uint32_t j_u32(const json& parent, const char* key, uint32_t def) {
    auto it = parent.find(key);
    if (it == parent.end() || it->is_null())
        return def;
    if (it->is_number_unsigned())
        return it->get<uint32_t>();
    if (it->is_number_integer())
        return static_cast<uint32_t>(it->get<int64_t>());
    if (it->is_number_float())
        return static_cast<uint32_t>(it->get<double>());
    return def;
}
static std::string j_str(const json& parent, const char* key, const std::string& def) {
    auto it = parent.find(key);
    if (it == parent.end() || it->is_null() || !it->is_string())
        return def;
    return it->get<std::string>();
}

DumpMaterial readMaterialJson(const std::string& filePath, const std::string& displayName) {
    DumpMaterial d;

    std::string text;
    if (!read_file_str(filePath, text)) {
        err("read_material: cannot read dump material '%s'", filePath.c_str());
        return d; // loaded=false
    }

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        err("read_material: JSON parse error in '%s': %s", filePath.c_str(), e.what());
        return d; // loaded=false
    }

    // --- top-level scalars (verbatim IW5 dump values) ---
    d.name = j_str(j, "name", displayName);
    d.techniqueSetName = j_str(j, "techniqueSet->name", "");
    d.gameFlags = j_int(j, "gameFlags", 0);
    d.sortKey = j_int(j, "sortKey", 0);
    d.stateFlags = j_int(j, "stateFlags", 0);
    d.cameraRegion = j_int(j, "cameraRegion", 0);
    d.surfaceTypeBits = j_u32(j, "surfaceTypeBits", 0);
    d.animationX = j_int(j, "animationX", 0);
    d.animationY = j_int(j, "animationY", 0);

    // --- maps[] : image refs + per-slot metadata ---
    auto mIt = j.find("maps");
    if (mIt != j.end() && mIt->is_array()) {
        for (const auto& e : *mIt) {
            DumpMap m;
            m.image = j_str(e, "image", "");
            m.semantic = j_int(e, "semantic", 0);
            m.sampleState = j_int(e, "sampleState", 0); // may be negative
            // IW5 dump uses "firstCharacter"/"lastCharacter"; tolerate the alt "secondLastCharacter".
            m.firstChar = j_int(e, "firstCharacter", 0);
            m.lastChar = e.contains("lastCharacter") ? j_int(e, "lastCharacter", 0)
                                                     : j_int(e, "secondLastCharacter", 0);
            m.typeHash = j_u32(e, "typeHash", 0);
            d.maps.push_back(std::move(m));
        }
    }

    // --- constantTable[] : may be array, null, or absent ---
    auto cIt = j.find("constantTable");
    if (cIt != j.end() && cIt->is_array()) {
        for (const auto& e : *cIt) {
            DumpConst c;
            c.name = j_str(e, "name", "");
            c.nameHash = j_u32(e, "nameHash", 0);
            auto lIt = e.find("literal");
            if (lIt != e.end() && lIt->is_array()) {
                for (int k = 0; k < 4 && k < static_cast<int>(lIt->size()); ++k) {
                    const auto& lv = (*lIt)[k];
                    c.literal[k] =
                        lv.is_null()
                            ? 0.f
                            : (lv.is_number() ? static_cast<float>(lv.get<double>()) : 0.f);
                }
            }
            d.constants.push_back(std::move(c));
        }
    }

    // --- stateMap[] : [ [loadBits0, loadBits1], ... ]  (informational; IW8 owns statebits via techset) ---
    auto sIt = j.find("stateMap");
    if (sIt != j.end() && sIt->is_array()) {
        for (const auto& e : *sIt) {
            DumpStateBits sb;
            if (e.is_array() && e.size() >= 2) {
                sb.loadBits[0] = e[0].is_number() ? static_cast<uint32_t>(e[0].get<int64_t>()) : 0;
                sb.loadBits[1] = e[1].is_number() ? static_cast<uint32_t>(e[1].get<int64_t>()) : 0;
            }
            d.stateMap.push_back(sb);
        }
    }

    d.loaded = !d.name.empty();
    if (!d.loaded)
        warn("read_material: '%s' parsed but has no 'name' key", filePath.c_str());
    else
        debug("read_material: '%s' -> %zu maps, %zu const, %zu stateMap, techset='%s'",
              d.name.c_str(), d.maps.size(), d.constants.size(), d.stateMap.size(),
              d.techniqueSetName.c_str());
    return d;
}

} // namespace convdump::mtl
