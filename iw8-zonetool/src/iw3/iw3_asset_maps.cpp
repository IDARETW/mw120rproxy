// asset_maps.cpp — Stage-A IW3 MAPS-family dumper (replaces the iw3_assets_stub.cpp 'maps' stub).
// Implements convert::iw3_dump_maps: extract the IW3 map's entityString + clipMap bounds + comworld +
// dynentity defs from the inflated IW3 zone and write them to the zone-source as:
//   maps/mp/<map>.d3dbsp.entityString   (IW8 numeric-keyId entity text, converted from IW3 string keys)
//   maps/mp/<map>.d3dbsp.map_ents       (MapEnts fields JSON: name + numEntityChars)
//   maps/mp/<map>.d3dbsp.col_map        (clipMap fields JSON: broadphase bounds + null-havok markers)
//   maps/mp/<map>.d3dbsp.com_map        (ComWorld fields JSON: valid-empty)
//   dynentitylist0.dynent.data          (binary DynentRecord[] — empty for the prototype)
// and records map_ents(29)/col_map(23)/com_map(24) in the manifest.
//
// OFFLINE SCAN RATIONALE (SPEC §2b): the foundation loader does not reproduce the full IW3 pointer-fixup
// graph yet, so we locate the entityString directly from the flat zone (it is a uniquely-identifiable
// contiguous ASCII region). clipMap collision (Havok) is NOT a prototype goal (null havok = noclip,
// memory iw8-124-map-consumer-arch); broadphase bounds default to a generous world AABB (matches the
// proven iw8zonewriter map_zone.h). This keeps Stage A robust + the output load-equivalent to the
// proven srv map zone.
#include "../convert/registry.h"
#include "../convert/maps_convert.h"
#include "../common/log.h"
#include "../common/json.hpp"
#include "iw3_zone.h"
#include "maps_read.h"
#include "../zonesrc/zone_source.h"
#include <cstring>

namespace iw3maps {

bool extractEntityString(const std::vector<uint8_t>& zone, std::string& outString) {
    outString.clear();
    // Anchor: the worldspawn classname pair. IW3 worldspawn is "\"classname\" \"worldspawn\"".
    static const char kMarker[] = "\"classname\" \"worldspawn\"";
    const size_t mlen = sizeof(kMarker) - 1;
    if (zone.size() < mlen) return false;

    // Find the marker.
    size_t hit = std::string::npos;
    for (size_t i = 0; i + mlen <= zone.size(); ++i) {
        if (std::memcmp(zone.data() + i, kMarker, mlen) == 0) { hit = i; break; }
    }
    if (hit == std::string::npos) { zt::warn("iw3 maps: worldspawn marker not found"); return false; }

    auto isText = [](uint8_t c) {
        return (c >= 32 && c < 127) || c == '\t' || c == '\n' || c == '\r';
    };
    // Expand backward to the first byte of the contiguous text region (the leading '{' of worldspawn).
    size_t start = hit;
    while (start > 0 && isText(zone[start - 1])) --start;
    // Expand forward to the end of the contiguous text region (stops at the NUL terminator).
    size_t end = hit;
    while (end < zone.size() && isText(zone[end])) ++end;

    // Trim to the bounding braces: from the first '{' to the last '}' inclusive.
    while (start < end && zone[start] != '{') ++start;
    size_t lastBrace = end;
    while (lastBrace > start && zone[lastBrace - 1] != '}') --lastBrace;
    if (lastBrace <= start) { zt::warn("iw3 maps: entityString braces not bounded"); return false; }

    outString.assign(reinterpret_cast<const char*>(zone.data()) + start, lastBrace - start);
    return true;
}

} // namespace iw3maps

namespace convert {

using nlohmann::json;

void iw3_dump_maps(iw3::LoadCtx& lc, iw3sr::ZoneSource& zs) {
    // The LoadCtx wraps the full inflated IW3 zone. Pull the raw bytes for the offline scan.
    std::vector<uint8_t> zone(lc.base(), lc.base() + lc.size());

    const std::string assetName = "maps/mp/" + zs.map() + ".d3dbsp";

    // 1) entityString: extract IW3 (string-key) text, convert to IW8 (numeric-keyId) text.
    std::string iw3Ents, iw8Ents;
    size_t entCount = 0;
    if (iw3maps::extractEntityString(zone, iw3Ents)) {
        entCount = iw3ToIw8EntityString(iw3Ents, iw8Ents);
        zt::info("iw3 maps: entityString IW3=%zu bytes -> IW8=%zu bytes (%zu entities)",
                 iw3Ents.size(), iw8Ents.size(), entCount);
    } else {
        zt::warn("iw3 maps: no entityString found; emitting empty map");
    }
    // Record entityString + map_ents in the zone-source (map_ents manifest row added by addMapSubText).
    zs.addMapSubText("map_ents", "entityString", iw8Ents);

    // 2) map_ents fields JSON (name + numEntityChars = strlen+1 of the IW8 entityString).
    {
        json j;
        j["name"] = assetName;
        j["numEntityChars"] = iw8Ents.size() + 1; // strlen + NUL (matches map_zone.h emitMapEntsBody)
        j["entityCount"] = entCount;
        j["numDynEntities"] = 0; // load-critical MapEnts emits 0 dynents (doc 10 minimal)
        zs.writeText(zs.mapSubPath("map_ents"), j.dump(2));
    }

    // 3) col_map (clipMap_t) fields JSON. The prototype uses null-havok collision + a generous world
    //    broadphase AABB (the proven map_zone.h shape). Records the values the IW8 writer stamps.
    {
        zs.manifestAdd("col_map", assetName);
        json j;
        j["name"] = assetName;
        j["isInUse"] = 1;
        j["broadphaseMin"] = { -100000.0, -100000.0, -100000.0 };
        j["broadphaseMax"] = {  100000.0,  100000.0,  100000.0 };
        j["havokWorldShapeDataSize"] = 0;     // null havok => noclip/void floor (prototype scope)
        j["havokWorldShapeDataNull"] = true;
        j["numStaticModelCollisionModelLists"] = 0;
        j["numSubModels"] = 0;
        j["checksum"] = 0;
        zs.writeText(zs.mapSubPath("col_map"), j.dump(2));
    }

    // 4) com_map (ComWorld) fields JSON — valid-empty (name + zeros; doc 10 §ComWorld "likely safe").
    {
        zs.manifestAdd("com_map", assetName);
        json j;
        j["name"] = assetName;
        j["isInUse"] = 1;
        j["primaryLightCount"] = 0;
        j["numUmbraGates"] = 0;
        zs.writeText(zs.mapSubPath("com_map"), j.dump(2));
    }

    // 5) dynentitylist0.dynent.data — empty for the prototype (the load-critical MapEnts emits 0
    //    dynents; the blob is recorded for downstream/fidelity per SPEC §3).
    {
        std::vector<DynentRecord> dynents; // empty
        zs.writeDynents(serializeDynents(dynents));
    }

    zt::info("iw3 maps: dumped map_ents+col_map+com_map+dynent for '%s'", assetName.c_str());
}

} // namespace convert
