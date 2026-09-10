#pragma once
#include <cstddef>
#include <cstdint>

// Integrated into the build (was reference/iw8_map_structs_pinned.h). Namespaced under iw8::maps so
// the pinned field maps + static_asserts compile-validate the load-critical sizes/offsets WITHOUT
// colliding with the offset-stamp constants in map_zone.h. The map zone emitter cross-references
// these offsets.
namespace iw8
{
namespace maps
{

#pragma pack(push, 1)

// ----- shared scalar helpers -----
struct vec2_t
{
    float v[2];
};
struct vec3_t
{
    float v[3];
};
struct Bounds
{
    vec3_t midPoint;
    vec3_t halfSize;
}; // 24

// ===========================================================================================
// clipMap_t  — col_map (23) — g_assetSizes 0xF8 (248). CONFIRMED(PDB), dev==retail.
// ===========================================================================================
struct MapTriggers
{                   // 0x50 (reused in clipMap_t.stageTrigger and MapEnts.trigger)
    uint32_t count; // 0x00
    uint32_t _pad04;
    void *models;       // 0x08  TriggerModel*   [count]
    uint32_t hullCount; // 0x10
    uint32_t _pad14;
    void *hulls;        // 0x18  TriggerHull*    [hullCount]
    uint32_t slabCount; // 0x20
    uint32_t _pad24;
    void *slabs;           // 0x28  TriggerSlab*    [slabCount]
    uint32_t windingCount; // 0x30
    uint32_t _pad34;
    void *windings;             // 0x38  TriggerWinding* [windingCount]
    uint32_t windingPointCount; // 0x40
    uint32_t _pad44;
    void *windingPoints; // 0x48  TriggerWindingPoint* [windingPointCount]
};
static_assert(sizeof(MapTriggers) == 0x50, "MapTriggers");

struct cmodel_t
{                  // 0x38
    Bounds bounds; // 0x00
    float radius;  // 0x18
    uint32_t _pad1C;
    void *physicsAsset;               // 0x20  PhysicsAsset* (Havok)
    uint16_t physicsShapeOverrideIdx; // 0x28
    uint16_t navObstacleIdx;          // 0x2A
    uint32_t edgeFirstIndex;          // 0x2C
    uint32_t edgeTotalCount;          // 0x30
    uint32_t _pad34;
};
static_assert(sizeof(cmodel_t) == 0x38, "cmodel_t");

struct clipMap_t
{                                               // 0xF8
    const char *name;                           // 0x00  PTR
    int32_t isInUse;                            // 0x08
    uint32_t numStaticModelCollisionModelLists; // 0x0C  CNT
    void *staticModelCollisionModelLists;       // 0x10  PTR[0x0C]
    void *mapEnts;                              // 0x18  PTR  (MapEnts*, cross-link)
    void *stages;                               // 0x20  PTR[stageCount]
    uint8_t stageCount;                         // 0x28
    uint8_t _pad29[7];                          //
    MapTriggers stageTrigger;                   // 0x30  (0x50)
    vec3_t broadphaseMin;                       // 0x80
    vec3_t broadphaseMax;                       // 0x8C
    uint8_t physicsCapacities[32];              // 0x98  PhysicsCapacities
    uint32_t havokWorldShapeDataSize;           // 0xB8  CNT
    uint32_t _padBC;
    void *havokWorldShapeData;           // 0xC0  PTR[0xB8] (null = NO collision)
    uint32_t numCollisionHeatmapEntries; // 0xC8  CNT
    uint32_t _padCC;
    void *collisionHeatmap;            // 0xD0  PTR[0xC8]
    uint32_t totalGlassInitPieceCount; // 0xD8
    uint32_t totalGlassPieceLimit;     // 0xDC
    void *topDownMapData;              // 0xE0  PTR
    const char *visionSetName;         // 0xE8  PTR
    uint32_t checksum;                 // 0xF0
    uint32_t _padF4;
};
static_assert(offsetof(clipMap_t, mapEnts) == 0x18, "clipMap_t.mapEnts");
static_assert(offsetof(clipMap_t, broadphaseMin) == 0x80, "clipMap_t.broadphaseMin");
static_assert(offsetof(clipMap_t, havokWorldShapeDataSize) == 0xB8, "clipMap_t.havokSize");
static_assert(offsetof(clipMap_t, havokWorldShapeData) == 0xC0, "clipMap_t.havokData");
static_assert(offsetof(clipMap_t, checksum) == 0xF0, "clipMap_t.checksum");
static_assert(sizeof(clipMap_t) == 0xF8, "clipMap_t == g_assetSizes[23]");

// ===========================================================================================
// MapEnts  — map_ents (29) — g_assetSizes 0x428 (1064). CONFIRMED(PDB), dev==retail.
// The dynentitylist lives inside this struct (the dynEnt* region @0x190..0x1EB).
// ===========================================================================================
struct SpawnPointRecordList
{
    uint32_t count;
    uint32_t _pad;
    void *records;
}; // 0x10

struct MapEnts
{                           // 0x428
    const char *name;       // 0x00  PTR
    char *entityString;     // 0x08  PTR[0x10] ⭐
    int32_t numEntityChars; // 0x10  CNT
    uint32_t _pad14;
    MapTriggers trigger;             // 0x18  (0x50)
    uint8_t clientTrigger[176];      // 0x68  ClientTriggers (inline; ptrs count-gated)
    uint8_t clientTriggerBlend[16];  // 0x118
    SpawnPointRecordList spawnList;  // 0x128 (0x10)
    uint8_t splineList[16];          // 0x138 SplineRecordList {count, PTR}
    uint32_t havokEntsShapeDataSize; // 0x148 CNT
    uint32_t _pad14C;
    char *havokEntsShapeData; // 0x150 PTR[0x148]
    uint32_t numSubModels;    // 0x158 CNT
    uint32_t _pad15C;
    void *cmodels;                   // 0x160 PTR[0x158] (cmodel_t*)
    uint32_t edgeListUsedQueryTypes; // 0x168
    uint32_t numEdgeLists;           // 0x16C CNT
    void *edgeLists;                 // 0x170 PTR[0x16C] (MapEdgeList**)
    void *edgeListSpatialTree;       // 0x178 PTR
    uint32_t numClientModels;        // 0x180 CNT
    uint32_t _pad184;
    void *clientModels;           // 0x188 PTR[0x180]
    uint32_t dynEntCount[2];      // 0x190
    uint32_t dynEntCountTotal;    // 0x198
    uint16_t dynEntityListsCount; // 0x19C
    uint16_t _pad19E;
    void *dynEntListIds;                   // 0x1A0 PTR
    uint16_t dynEntPhysicsSetupHead[2][2]; // 0x1A8
    uint16_t dynEntPhysicsSetupTail[2][2]; // 0x1B0
    uint16_t dynEntNoSpatialCount[2];      // 0x1B8
    uint16_t dynEntMaxClientHistoryCount;  // 0x1BC
    uint8_t dynEntMaxPosePartCount;        // 0x1BE
    uint8_t _pad1BF;
    uint32_t dynEntsWithExtraPosePartsCount; // 0x1C0
    uint32_t _pad1C4;
    void *dynEntSpatialPopulation[2];   // 0x1C8 PTR×2
    void *dynEntSpatialTransientMap[2]; // 0x1D8 PTR×2
    uint32_t dynEntNoSpatialList;       // 0x1E8
    uint32_t clientEntAnchorCount;      // 0x1EC CNT
    void *clientEntAnchors;             // 0x1F0 PTR[0x1EC]
    uint8_t scriptableMapEnts[216];     // 0x1F8 (inline; own PTR/CNT)
    uint8_t spawnGroupLoot[48];         // 0x2D0
    uint8_t clientSideEffects[152];     // 0x300 (inline; PTR/CNT)
    uint8_t createFxAssetData[16];      // 0x398
    uint32_t exploderNameTotal;         // 0x3A8 CNT
    uint32_t _pad3AC;
    void *exploderNames;           // 0x3B0 PTR[0x3A8]
    uint8_t serverSideEffects[16]; // 0x3B8 (inline; PTR/CNT)
    uint32_t createFxEffectTotal;  // 0x3C8
    uint32_t numMayhemScenes;      // 0x3CC CNT
    void *mayhemScenes;            // 0x3D0 PTR[0x3CC]
    uint8_t spawners[16];          // 0x3D8 SpawnerList {count, PTR}
    uint32_t audioPASpeakerCount;  // 0x3E8 CNT
    uint32_t _pad3EC;
    void *audioPASpeakers;      // 0x3F0 PTR[0x3E8]
    uint32_t numAudioPropNodes; // 0x3F8 CNT
    uint32_t _pad3FC;
    void *audioPropNodes;       // 0x400 PTR[0x3F8]
    uint32_t numAudioPropEdges; // 0x408 CNT
    uint32_t _pad40C;
    void *audioPropEdges; // 0x410 PTR[0x408]
    uint32_t numCollmaps; // 0x418 CNT
    uint32_t _pad41C;
    void *collmapLookups; // 0x420 PTR[0x418]
};
static_assert(offsetof(MapEnts, entityString) == 0x08, "MapEnts.entityString");
static_assert(offsetof(MapEnts, numEntityChars) == 0x10, "MapEnts.numEntityChars");
static_assert(offsetof(MapEnts, trigger) == 0x18, "MapEnts.trigger");
static_assert(offsetof(MapEnts, clientTrigger) == 0x68, "MapEnts.clientTrigger");
static_assert(offsetof(MapEnts, spawnList) == 0x128, "MapEnts.spawnList");
static_assert(offsetof(MapEnts, numSubModels) == 0x158, "MapEnts.numSubModels");
static_assert(offsetof(MapEnts, cmodels) == 0x160, "MapEnts.cmodels");
static_assert(offsetof(MapEnts, collmapLookups) == 0x420, "MapEnts.collmapLookups");
static_assert(sizeof(MapEnts) == 0x428, "MapEnts == g_assetSizes[29]");

// ===========================================================================================
// ComWorld  — com_map (24) — g_assetSizes 0xA8 (168). CONFIRMED(PDB), dev==retail.
// ===========================================================================================
struct ComWorld
{                               // 0xA8
    const char *name;           // 0x00 PTR
    int32_t isInUse;            // 0x08
    int32_t useForwardPlus;     // 0x0C
    uint32_t bakeQuality;       // 0x10
    int32_t proxyLODQuality;    // 0x14
    float trVisRadii[5];        // 0x18
    float trVisFacingDistAdd;   // 0x2C
    uint32_t primaryLightCount; // 0x30 CNT
    uint32_t _pad34;
    void *primaryLights;                  // 0x38 PTR[0x30] (ComPrimaryLight* — element 0xA0)
    uint32_t scriptablePrimaryLightCount; // 0x40
    uint32_t firstScriptablePrimaryLight; // 0x44
    uint32_t transientTableSize;          // 0x48 CNT
    uint32_t _pad4C;
    void *transientTable;       // 0x50 PTR[0x48] (u16*)
    uint8_t changeListInfo[16]; // 0x58 ComChangeListInfo
    uint32_t numUmbraGates;     // 0x68 CNT
    uint32_t _pad6C;
    void *umbraGateNames;               // 0x70 PTR[0x68] (const char**)
    uint8_t umbraGateInitialStates[48]; // 0x78 bitarray<384>
};
static_assert(offsetof(ComWorld, primaryLightCount) == 0x30, "ComWorld.primaryLightCount");
static_assert(offsetof(ComWorld, primaryLights) == 0x38, "ComWorld.primaryLights");
static_assert(sizeof(ComWorld) == 0xA8, "ComWorld == g_assetSizes[24]");

// ===========================================================================================
// GfxWorld  — gfx_map (31) — g_assetSizes 0x45D0 (17872) RETAIL.
// dev PDB sizeof = 0x41E0 → retail is +0x3F0 LARGER (inserted in the post-`draw` light/dpvs
// region). HEAD (name..draw) is byte-identical dev<->retail (CONFIRMED against the real
// mp_m_overunder instance). The tail is opaque-padded so sizeof == 0x45D0 and checksum lands at the
// proven retail offset 0x3E60 (= dev 0x3A70 + 0x3F0; gfx[0x3E60]==col_map[0xF0] over 3 real
// samples).
//
// ⚠ RENDER-SAFETY: an ALL-ZEROS GfxWorld is NOT render-safe — R_SetupDpvsForPoint does
// `for(i=*dpvsPlanes.nodes; ...)` with NO null/count guard, so nodes==NULL => crash. map: either
// OMIT gfx_map, or emit the minimal-VALID form (cellCount=1, 1 GfxCell, nodeCount=1, terminating
// nodes[]). See the .md §5. Do NOT memset-0 and ship.
// ===========================================================================================
struct GfxWorldDpvsPlanes
{                        // 0x28  (render-critical) — CONFIRMED(instance)
    uint32_t cellCount;  // 0x00
    uint16_t planeCount; // 0x04
    uint16_t _pad06;
    void *planes;       // 0x08 PTR (GfxWorldDpvsPlane*)
    uint16_t nodeCount; // 0x10
    uint8_t _pad12[6];
    void *nodes;            // 0x18 PTR (u16*) <- R_SetupDpvsForPoint derefs *nodes
    void *sceneEntCellBits; // 0x20 PTR (u32*)
};
static_assert(sizeof(GfxWorldDpvsPlanes) == 0x28, "GfxWorldDpvsPlanes");

struct GfxCell
{                         // 0x28
    Bounds bounds;        // 0x00
    uint16_t portalCount; // 0x18
    uint8_t _pad1A[6];
    void *portals; // 0x20 PTR[portalCount]
};
static_assert(sizeof(GfxCell) == 0x28, "GfxCell");

struct GfxWorld
{                                  // 0x45D0
    const char *name;              // 0x00 PTR
    const char *baseName;          // 0x08 PTR
    int32_t bspVersion;            // 0x10 (=243 in real instances)
    uint8_t _lightSortBlock[0x64]; // 0x14..0x77 light counts + sortKey* (scalar, no walk)
    Bounds bounds;                 // 0x78 (24)
    GfxWorldDpvsPlanes dpvsPlanes; // 0x90 (0x28) — cells/nodes here
    void *cells;                   // 0xB8 PTR[dpvsPlanes.cellCount]
    void *cellTransientInfos;      // 0xC0 PTR[cellCount]
    uint8_t surfaces[168];         // 0xC8 GfxWorldSurfaces (surfaceCount = first u32)
    uint8_t smodels[792];          // 0x170 GfxWorldStaticModels
    uint8_t draw[12800];           // 0x488 GfxWorldDraw (0x3200; ends 0x3688)
    // ---- post-draw region: dev offsets here; RETAIL inserts +0x3F0 somewhere below. Treat opaque.
    // ----
    uint8_t _retailTail[0x45D0 - 0x3688]; // 0x3688..0x45D0 (= 0xF48). checksum is inside @0x3E60.
};
static_assert(offsetof(GfxWorld, baseName) == 0x08, "GfxWorld.baseName");
static_assert(offsetof(GfxWorld, bspVersion) == 0x10, "GfxWorld.bspVersion");
static_assert(offsetof(GfxWorld, dpvsPlanes) == 0x90, "GfxWorld.dpvsPlanes");
static_assert(offsetof(GfxWorld, cells) == 0xB8, "GfxWorld.cells");
static_assert(offsetof(GfxWorld, surfaces) == 0xC8, "GfxWorld.surfaces");
static_assert(offsetof(GfxWorld, draw) == 0x488, "GfxWorld.draw");
static_assert(sizeof(GfxWorld) == 0x45D0, "GfxWorld == g_assetSizes[31]");

// Retail checksum offset (NOT a struct member here because the tail is opaque-padded):
//   GFXWORLD_RETAIL_CHECKSUM_OFFSET = 0x3E60  (proven: gfx[0x3E60] == col_map[0xF0], 3 real
//   samples)
static constexpr size_t GFXWORLD_RETAIL_CHECKSUM_OFFSET = 0x3E60;

#pragma pack(pop)

} // namespace maps
} // namespace iw8
