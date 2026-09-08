
#pragma once
#include "iw8_structs.h"
#include "replay_map_layout.h"
#include "replay_netconst.h"
#include "iw8_map_structs.h" // pinned clipMap_t/MapEnts/ComWorld field maps (compile-validates sizes)
#include "iw8_zonebuffer.h"
#include "replay_spawns.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace iw8 {

// fixed sizes (g_assetSizes @0x4A9D7F0) — cross-checked vs iw8::maps:: pinned sizeofs at compile time.
static constexpr size_t kSizeClipMap = 0xF8;
static constexpr size_t kSizeMapEnts = replaymap::MapEntsSize;
static constexpr size_t kSizeComWorld = 0xA8;
static_assert(kSizeClipMap == sizeof(iw8::maps::clipMap_t), "clipMap size drift");

static_assert(kSizeComWorld == sizeof(iw8::maps::ComWorld), "ComWorld size drift");

// clipMap_t field offsets
static constexpr size_t kCM_name = 0x00, kCM_isInUse = 0x08, kCM_mapEnts = 0x18, kCM_bpMin = 0x80,
                        kCM_bpMax = 0x8C, kCM_havokSize = 0xB8, kCM_havokData = 0xC0,
                        kCM_checksum = 0xF0;
// MapEnts field offsets
static constexpr size_t kME_name = 0x00, kME_entityString = 0x08, kME_numEntityChars = 0x10;
// ComWorld field offsets (com_map(24)=0xA8) — pinned: name@0, isInUse@8, primaryLightCount@0x30.
static constexpr size_t kCW_name = 0x00, kCW_isInUse = 0x08, kCW_primaryLightCount = 0x30;
static_assert(offsetof(iw8::maps::ComWorld, name) == kCW_name, "ComWorld.name off");
static_assert(offsetof(iw8::maps::ComWorld, isInUse) == kCW_isInUse, "ComWorld.isInUse off");
static_assert(offsetof(iw8::maps::ComWorld, primaryLightCount) == kCW_primaryLightCount,
              "ComWorld.plc off");

inline void stamp64(uint8_t* p, size_t off, uint64_t v) {
    std::memcpy(p + off, &v, 8);
}
inline void stamp32(uint8_t* p, size_t off, uint32_t v) {
    std::memcpy(p + off, &v, 4);
}
inline void stampf(uint8_t* p, size_t off, float v) {
    std::memcpy(p + off, &v, 4);
}

// Emit one MapEnts asset body: struct(0x408)->TEMP_PRELOAD(1), then name + entityString -> VIRTUAL(8).
// Caller is responsible for the surrounding push/pop. `ents` is the IW8 numeric-keyId entityString (NUL added here).
inline void emitMapEntsBody(ZoneBuffer& zb,
                            const char* assetName,
                            const std::string& ents,
                            const ReplaySpawns* spawns = nullptr) {
    uint8_t me[kSizeMapEnts];
    std::memset(me, 0, sizeof(me));
    stamp64(me, kME_name, PTR_FOLLOWS);
    stamp64(me, kME_entityString, PTR_FOLLOWS);
    stamp32(me, kME_numEntityChars, (uint32_t)(ents.size() + 1)); // strlen + NUL
    if (spawns && !spawns->records.empty()) {
        stamp32(me, 0x128, static_cast<uint32_t>(spawns->records.size()));
        stamp64(me, 0x130, PTR_FOLLOWS);
    }
    // Both DynEnt spatial populations are queried on the first client frame,
    // even with no dynamic entities. Replay DDFC10/DDFAE0: two inline 56-byte
    // populations, each with one empty leaf and its -1 bucket sentinel.
    stamp64(me, 0x1C8, PTR_FOLLOWS);
    stamp64(me, 0x1D0, PTR_FOLLOWS);
    stamp32(me, 0x1E8, 0xFFFFFFFFu); // empty dynEntNoSpatialList node
    // ScriptableCl spatial initialization (12EC5F0) needs a tree even when
    // there are no authored instances: spawned characters use reserved slots.
    // A null tree exits before allocating their instance-to-marker table.
    stamp64(me, 0x2C0, PTR_FOLLOWS);
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(me, sizeof(me));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // name
    zb.write(ents.data(), ents.size());
    zb.writeT<uint8_t>(0); // entityString + NUL
    if (spawns)
        spawns->writeRecords(zb);
    for (unsigned i = 0; i < 2; ++i) {
        zb.align(7);
        uint8_t population[0x38]{};
        stamp64(population, 0, PTR_FOLLOWS);
        stamp64(population, 8, PTR_FOLLOWS);
        stamp32(population, 0x28, 1);
        zb.write(population, sizeof(population));
        // DDFAE0 -> DDFE70. GatherPartitions reads tree->extents before
        // advancing the iterator; a null tree cannot represent empty data.
        zb.align(7);
        uint8_t tree[24]{};
        stamp64(tree, 0, PTR_FOLLOWS);
        stamp64(tree, 8, PTR_FOLLOWS);
        stamp32(tree, 16, 1);
        zb.write(tree, sizeof(tree));
        zb.align(3);
        zb.writeT<uint64_t>(0); // unsplit leaf partition
        zb.align(3);
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(-100000.f);
        for (unsigned k = 0; k < 3; ++k)
            zb.writeT<float>(100000.f);
        zb.align(3);
        zb.writeT<uint32_t>(0xFFFFFFFFu);
    }
    // Load_ScriptableMapEnts DBCF30 -> DDFE70 (one empty spatial leaf).
    zb.align(7);
    uint8_t scriptableTree[24]{};
    stamp64(scriptableTree, 0, PTR_FOLLOWS);
    stamp64(scriptableTree, 8, PTR_FOLLOWS);
    stamp32(scriptableTree, 16, 1);
    zb.write(scriptableTree, sizeof(scriptableTree));
    zb.align(3);
    zb.writeT<uint64_t>(0);
    zb.align(3);
    for (unsigned k = 0; k < 3; ++k)
        zb.writeT<float>(-100000.f);
    for (unsigned k = 0; k < 3; ++k)
        zb.writeT<float>(100000.f);
    zb.popStream();
    zb.popStream();
}

// Emit srv_<map> zone: map_ents(29) + col_map(23). assetName = "maps/mp/<map>.d3dbsp".
inline void buildMapZone(ZoneBuffer& zb, const char* assetName, const std::string& ents) {
    // 1) XAssetList root -> TEMP(0)
    zb.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = 0;
    al.stringList.loaded = 0;
    al.stringList.strings = PTR_NULL;
    al.assetCount = 2;
    al.assetReadPos = 0;
    al.assets = PTR_FOLLOWS;
    zb.writeT(al);
    zb.popStream();

    // 2) XAsset[2] -> VIRTUAL(8): [0] map_ents(29), [1] col_map(23). header=-3 (insert).
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.align(7);
    IW8_XAsset a0{};
    a0.type = ASSET_TYPE_MAP_ENTS;
    a0.header = PTR_INSERT;
    zb.writeT(a0);
    IW8_XAsset a1{};
    a1.type = ASSET_TYPE_COL_MAP;
    a1.header = PTR_INSERT;
    zb.writeT(a1);

    // 3a) asset[0] map_ents(29): the standalone MapEnts
    zb.align(7);
    zb.reserveCalc(8); // native DB_InsertPointer slot
    emitMapEntsBody(zb, assetName, ents);

    // 3b) asset[1] col_map(23): clipMap struct -> TEMP_PRELOAD(1), name -> VIRTUAL(8), then mapEnts(-2) inline MapEnts
    zb.align(7);
    zb.reserveCalc(8); // top-level clipMap DB_InsertPointer slot
    uint8_t cm[kSizeClipMap];
    std::memset(cm, 0, sizeof(cm));
    stamp64(cm, kCM_name, PTR_FOLLOWS);
    stamp32(cm, kCM_isInUse, 1);
    stamp64(cm, kCM_mapEnts, PTR_FOLLOWS); // follows inline (duplicate MapEnts after the clipMap)
    // generous world AABB (covers the spawns; broadphase only)
    stampf(cm, kCM_bpMin + 0, -100000.f);
    stampf(cm, kCM_bpMin + 4, -100000.f);
    stampf(cm, kCM_bpMin + 8, -100000.f);
    stampf(cm, kCM_bpMax + 0, 100000.f);
    stampf(cm, kCM_bpMax + 4, 100000.f);
    stampf(cm, kCM_bpMax + 8, 100000.f);
    stamp32(cm, kCM_havokSize, 0);
    stamp64(cm, kCM_havokData, PTR_NULL); // no collision data
    stamp32(cm, kCM_checksum, 0);

    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(cm, sizeof(cm));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // clipMap name
    zb.popStream();
    // mapEnts = -2 => the loader reads a MapEnts inline right here (duplicate)
    emitMapEntsBody(zb, assetName, ents);
    zb.popStream();

    zb.popStream(); // VIRTUAL (XAsset array) -> TEMP
}

// ---------------------------------------------------------------------------------------------------
// Structural Replay srv zone: map_ents(29) + col_map(23) + com_map(24, valid-empty).
// This is the extension of buildMapZone that adds com_map (research doc 10/14b: com_map is NOT
// omittable; valid-empty = name + zeros). Optional `bounds` overrides the generous broadphase AABB
// when the dump's colmap verts gave a tight one. All three structs are zeroed buffers with named-field
// stamps validated against the iw8::maps:: pinned offsets.
struct MapBounds {
    float mn[3];
    float mx[3];
    bool valid = false;
};
struct MapSun {
    float intensity = 1.2f;
    float color[3]{1, .98f, .95f};
    float direction[3]{.6435823f, .5794841f, .5f};
    float up[3]{-.3715724f, -.3345653f, .8660254f};
};

// Emit one ComWorld asset body (com_map 24, valid-empty): struct(0xA8)->TEMP_PRELOAD(1), name->VIRTUAL(8).
// primaryLights left null (count=0) -> the loader allocates nothing; name + isInUse is the safe minimum.
inline void emitComWorldBody(ZoneBuffer& zb, const char* assetName, const MapSun& lighting = {}) {
    uint8_t cw[kSizeComWorld];
    std::memset(cw, 0, sizeof(cw));
    stamp64(cw, kCW_name, PTR_FOLLOWS);
    stamp32(cw, kCW_isInUse, 1);
    // Primary light zero is the reserved unlit entry. Replay's light-copy caller
    // passes [0, count-1] without a zero-count guard (18D9690).
    stamp32(cw, kCW_primaryLightCount, 2);
    stamp64(cw, 0x38, PTR_FOLLOWS);
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(cw, sizeof(cw));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // name
    zb.align(7);
    const uint8_t primaryLight[0xA0]{}; // Replay Load_ComWorld DF27B0
    zb.write(primaryLight, sizeof(primaryLight));
    // Authored daylight for the test room. Replay R_InitSelectedPrimaryLights
    // 18D96C0 copies type+1, intensity+10, color+20 and direction+2C.
    // Light zero remains reserved; stage zero selects this sun at index one.
    // Direction follows mp_test.map's -30 pitch / 42 heading. IW8 uses a
    // separate linear color and physical intensity; use neutral test daylight.
    uint8_t sun[0xA0]{};
    sun[1] = 1;
    stampf(sun, 0x10, lighting.intensity);
    for (unsigned k = 0; k < 3; ++k) {
        stampf(sun, 0x20 + 4 * k, lighting.color[k]);
        stampf(sun, 0x2C + 4 * k, lighting.direction[k]);
        stampf(sun, 0x38 + 4 * k, lighting.up[k]);
    }
    stampf(sun, 0x88, 1.0f);
    zb.write(sun, sizeof(sun));
    zb.popStream();
    zb.popStream();
}

inline void emitLevelNetConstStrings(ZoneBuffer& zb, unsigned type) {
    const std::string name = std::string("ncs_") + replayncs::Tags[type] + "_level";
    uint8_t body[replayncs::BodySize]{};
    stamp64(body, 0, PTR_FOLLOWS);
    stamp32(body, 8, type);
    stamp32(body, 12, replayncs::LevelSource);
    zb.align(7);
    zb.reserveCalc(8); // top-level DB_InsertPointer VIRTUAL slot
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(body, sizeof(body));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(name.c_str());
    zb.popStream();
    zb.popStream();
    zb.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zb.align(7);
    zb.reserveCalc(sizeof(body));
    zb.align(31);
    zb.popStream();
}

inline void buildSrvMapZone(ZoneBuffer& zb,
                            const char* assetName,
                            const std::string& ents,
                            const MapBounds& bounds,
                            const MapSun& lighting = {}) {
    const ReplaySpawns spawns(ents);
    // 1) XAssetList root -> TEMP(0): 3 assets.
    zb.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = spawns.records.empty() ? 0 : static_cast<uint32_t>(spawns.strings.size());
    al.stringList.loaded = 0;
    al.stringList.strings = al.stringList.count ? PTR_FOLLOWS : PTR_NULL;
    al.assetCount = 3 + replayncs::Count;
    al.assetReadPos = 0;
    al.assets = PTR_FOLLOWS;
    zb.writeT(al);
    zb.popStream();

    // 2) XAsset[3] -> VIRTUAL(8): [0] map_ents(29), [1] col_map(23), [2] com_map(24). header=-3.
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    spawns.writeStrings(zb);
    zb.align(7);
    IW8_XAsset a0{};
    a0.type = ASSET_TYPE_MAP_ENTS;
    a0.header = PTR_INSERT;
    zb.writeT(a0);
    IW8_XAsset a1{};
    a1.type = ASSET_TYPE_COL_MAP;
    a1.header = PTR_INSERT;
    zb.writeT(a1);
    IW8_XAsset a2{};
    a2.type = ASSET_TYPE_COM_MAP;
    a2.header = PTR_INSERT;
    zb.writeT(a2);
    for (unsigned type = 0; type < replayncs::Count; ++type) {
        IW8_XAsset asset{};
        asset.type = static_cast<IW8_XAssetType>(replayncs::AssetType);
        asset.header = PTR_INSERT;
        zb.writeT(asset);
    }

    // 3a) map_ents(29)
    zb.align(7);
    zb.reserveCalc(8); // native DB_InsertPointer slot
    emitMapEntsBody(zb, assetName, ents, &spawns);

    // 3b) col_map(23): clipMap struct -> TEMP_PRELOAD, name -> VIRTUAL, mapEnts(-2) inline MapEnts.
    zb.align(7);
    zb.reserveCalc(8); // top-level clipMap DB_InsertPointer slot
    uint8_t cm[kSizeClipMap];
    std::memset(cm, 0, sizeof(cm));
    stamp64(cm, kCM_name, PTR_FOLLOWS);
    stamp32(cm, kCM_isInUse, 1);
    stamp64(cm, kCM_mapEnts, PTR_FOLLOWS);
    stamp64(cm, 0x20, PTR_FOLLOWS);
    cm[0x28] = 1; // R_UpdateActiveStage 19864E0 always copies stage zero.
    // broadphase AABB: tight from the dump's colmap verts when available, else proven generous box.
    if (bounds.valid) {
        for (int k = 0; k < 3; ++k) {
            stampf(cm, kCM_bpMin + (size_t)k * 4, bounds.mn[k]);
            stampf(cm, kCM_bpMax + (size_t)k * 4, bounds.mx[k]);
        }
    } else {
        stampf(cm, kCM_bpMin + 0, -100000.f);
        stampf(cm, kCM_bpMin + 4, -100000.f);
        stampf(cm, kCM_bpMin + 8, -100000.f);
        stampf(cm, kCM_bpMax + 0, 100000.f);
        stampf(cm, kCM_bpMax + 4, 100000.f);
        stampf(cm, kCM_bpMax + 8, 100000.f);
    }
    stamp32(cm, kCM_havokSize, 0);
    stamp64(cm, kCM_havokData, PTR_NULL); // no collision data
    stamp32(cm, kCM_checksum, 0);

    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(cm, sizeof(cm));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // clipMap name
    zb.popStream();
    emitMapEntsBody(zb, assetName, ents, &spawns); // mapEnts=-2 duplicate
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.align(7);
    // Load_clipMap_t E0FD30 -> Load_StageArray E05480, 40 bytes.
    uint8_t stage[40]{};
    stamp64(stage, 0, PTR_FOLLOWS);
    stage[0x16] = 1;
    zb.write(stage, sizeof(stage));
    zb.writeStr("default");
    zb.popStream();
    zb.popStream();

    // 3c) com_map(24): ComWorld valid-empty
    zb.align(7);
    zb.reserveCalc(8); // top-level ComWorld DB_InsertPointer slot
    emitComWorldBody(zb, assetName, lighting);

    for (unsigned type = 0; type < replayncs::Count; ++type)
        emitLevelNetConstStrings(zb, type);

    zb.popStream(); // VIRTUAL (XAsset array) -> TEMP
}

} // namespace iw8
