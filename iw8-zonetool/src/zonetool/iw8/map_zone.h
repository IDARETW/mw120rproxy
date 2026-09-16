#pragma once
#include "iw8_map_structs.h" // pinned Replay 1.20 map field maps and sizes
#include "iw8_structs.h"
#include "iw8_zonebuffer.h"
#include "replay_havok.h"
#include "replay_map_layout.h"
#include "replay_netconst.h"
#include "replay_spawns.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace iw8
{
static_assert(ASSET_TYPE_NET_CONST_STRINGS == replayncs::AssetType,
              "Replay net-const-string asset ordinal");

// fixed sizes (g_assetSizes @0x4A9D7F0) — cross-checked vs iw8::maps:: pinned sizeofs at compile
// time.
static constexpr size_t kSizeClipMap = 0xF8;
static constexpr size_t kSizeMapEnts = replaymap::MapEntsSize;
static constexpr size_t kSizeComWorld = 0xA8;
static_assert(kSizeClipMap == sizeof(iw8::maps::clipMap_t), "clipMap size drift");
static_assert(kSizeMapEnts == sizeof(iw8::maps::MapEnts), "MapEnts size drift");
static_assert(kSizeComWorld == sizeof(iw8::maps::ComWorld), "ComWorld size drift");

// clipMap_t field offsets
static constexpr size_t kCM_name = 0x00, kCM_isInUse = 0x08, kCM_mapEnts = 0x18, kCM_bpMin = 0x80,
                        kCM_bpMax = 0x8C, kCM_havokSize = 0xB8, kCM_havokData = 0xC0,
                        kCM_glassInitCount = 0xD8, kCM_glassPieceLimit = 0xDC, kCM_checksum = 0xF0;
// MapEnts field offsets
static constexpr size_t kME_name = 0x00, kME_entityString = 0x08, kME_numEntityChars = 0x10,
                        kME_trigger = 0x18;
static constexpr size_t kME_havokEntsShapeDataSize = 0x148, kME_havokEntsShapeData = 0x150,
                        kME_numSubModels = 0x158, kME_cmodels = 0x160,
                        kME_edgeListUsedQueryTypes = 0x168, kME_numEdgeLists = 0x16C,
                        kME_edgeLists = 0x170, kME_edgeListSpatialTree = 0x178,
                        kME_dynEntCount = 0x190, kME_dynEntCountTotal = 0x198,
                        kME_dynEntityListsCount = 0x19C, kME_dynEntListIds = 0x1A0,
                        kME_dynEntNoSpatialCount = 0x1B8, kME_dynEntMaxClientHistoryCount = 0x1BC,
                        kME_dynEntMaxPosePartCount = 0x1BE;
// ComWorld field offsets (com_map(24)=0xA8) — pinned: name@0, isInUse@8, primaryLightCount@0x30.
static constexpr size_t kCW_name = 0x00, kCW_isInUse = 0x08, kCW_primaryLightCount = 0x30;
static_assert(offsetof(iw8::maps::ComWorld, name) == kCW_name, "ComWorld.name off");
static_assert(offsetof(iw8::maps::ComWorld, isInUse) == kCW_isInUse, "ComWorld.isInUse off");
static_assert(offsetof(iw8::maps::ComWorld, primaryLightCount) == kCW_primaryLightCount,
              "ComWorld.plc off");

inline void stamp64(uint8_t *p, size_t off, uint64_t v)
{
    std::memcpy(p + off, &v, 8);
}
inline void stamp32(uint8_t *p, size_t off, uint32_t v)
{
    std::memcpy(p + off, &v, 4);
}
inline void stamp16(uint8_t *p, size_t off, uint16_t v)
{
    std::memcpy(p + off, &v, 2);
}
inline void stampf(uint8_t *p, size_t off, float v)
{
    std::memcpy(p + off, &v, 4);
}

inline uint64_t packOffset(const uint32_t stream, const uint64_t offset)
{
    if (offset >= std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Replay packed offset exceeds 32 bits");
    return (static_cast<uint64_t>(stream & 0xF) << 32) | (offset + 1);
}

inline void emitPhysicsAssetReference(ZoneBuffer &zb,
                                      const char *name = ",scriptbrushmodeldummydefault")
{
    uint8_t asset[0x58]{};
    stamp64(asset, 0, PTR_FOLLOWS);
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(asset, sizeof(asset));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(name);
    zb.popStream();
    zb.popStream();
}

inline void emitMapTriggers(ZoneBuffer &zb, const havok::BakeResult &collision)
{
    if (collision.triggers.empty())
        return;
    if (collision.triggers.size() > std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("Replay trigger-model count exceeds 16 bits");

    std::vector<std::vector<const havok::CollisionHull *>> hulls(collision.triggers.size());
    std::size_t totalHulls = 0;
    std::size_t totalSlabs = 0;
    for (std::size_t triggerIndex = 0; triggerIndex < collision.triggers.size(); ++triggerIndex)
    {
        const auto &trigger = collision.triggers[triggerIndex];
        if (!trigger.staticPhysics || trigger.collisionModel == 0 ||
            trigger.collisionModel >= collision.models.size())
            throw std::runtime_error("Replay trigger model is invalid");
        for (const auto &hull : collision.hulls)
            if (hull.model == trigger.collisionModel)
            {
                hulls[triggerIndex].push_back(&hull);
                ++totalHulls;
                totalSlabs += hull.slabs.size();
            }
        if (hulls[triggerIndex].empty())
            throw std::runtime_error("Replay trigger model has no collision hulls");
    }
    if (totalHulls > std::numeric_limits<uint16_t>::max() ||
        totalSlabs > std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("Replay trigger geometry exceeds 16-bit indices");

    zb.align(7);
    const uint64_t arrayStart = zb.streamSize(XFILE_BLOCK_VIRTUAL);
    const uint64_t insertionOffset =
        (arrayStart + collision.triggers.size() * sizeof(maps::TriggerModel) + 7) & ~uint64_t{7};
    const uint64_t alias = packOffset(XFILE_BLOCK_VIRTUAL, insertionOffset);
    uint16_t firstHull = 0;
    for (std::size_t index = 0; index < collision.triggers.size(); ++index)
    {
        maps::TriggerModel model{};
        model.contents = 1;
        model.hullCount = static_cast<uint16_t>(hulls[index].size());
        model.firstHull = firstHull;
        model.physicsAsset = reinterpret_cast<void *>(index == 0 ? PTR_INSERT : alias);
        model.physicsShapeOverrideIdx =
            collision.models[collision.triggers[index].collisionModel].shapeIndex;
        zb.writeT(model);
        firstHull = static_cast<uint16_t>(firstHull + model.hullCount);
    }
    zb.align(7);
    if (zb.streamSize(XFILE_BLOCK_VIRTUAL) != insertionOffset)
        throw std::runtime_error("Replay trigger physics insertion offset drifted");
    zb.reserveCalc(8);
    emitPhysicsAssetReference(zb, ",triggermodelstaticdummydefault");

    zb.align(3);
    uint16_t firstSlab = 0;
    for (const auto &group : hulls)
        for (const auto *source : group)
        {
            maps::TriggerHull hull{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                hull.bounds.midPoint.v[axis] =
                    (source->minimum[axis] + source->maximum[axis]) * 0.5f;
                hull.bounds.halfSize.v[axis] =
                    (source->maximum[axis] - source->minimum[axis]) * 0.5f;
            }
            hull.contents = 1;
            hull.slabCount = static_cast<uint16_t>(source->slabs.size());
            hull.firstSlab = firstSlab;
            zb.writeT(hull);
            firstSlab = static_cast<uint16_t>(firstSlab + hull.slabCount);
        }

    if (totalSlabs)
    {
        zb.align(3);
        for (const auto &group : hulls)
            for (const auto *source : group)
                for (const auto &sourceSlab : source->slabs)
                {
                    maps::TriggerSlab slab{};
                    std::copy(sourceSlab.direction.begin(), sourceSlab.direction.end(),
                              slab.direction.v);
                    slab.midpoint = sourceSlab.midpoint;
                    slab.halfSize = sourceSlab.halfSize;
                    zb.writeT(slab);
                }
    }
}

inline void emitCmodels(ZoneBuffer &zb, const havok::BakeResult &collision)
{
    if (collision.models.empty())
        return;
    if (collision.models.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Replay cmodel count exceeds 32 bits");

    zb.align(7);
    const uint64_t arrayStart = zb.streamSize(XFILE_BLOCK_VIRTUAL);
    const uint64_t insertionOffset =
        (arrayStart + collision.models.size() * sizeof(maps::cmodel_t) + 7) & ~uint64_t{7};
    const uint64_t alias = packOffset(XFILE_BLOCK_VIRTUAL, insertionOffset);
    bool insertedReference = false;
    for (std::size_t index = 0; index < collision.models.size(); ++index)
    {
        const auto &model = collision.models[index];
        uint8_t cmodel[sizeof(maps::cmodel_t)]{};
        float radiusSquared = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float midpoint = (model.minimum[axis] + model.maximum[axis]) * 0.5f;
            const float halfSize = (model.maximum[axis] - model.minimum[axis]) * 0.5f;
            if (!std::isfinite(midpoint) || !std::isfinite(halfSize) || halfSize < 0.0f)
                throw std::runtime_error("Replay cmodel has invalid bounds");
            stampf(cmodel, axis * 4, midpoint);
            stampf(cmodel, 12 + axis * 4, halfSize);
            radiusSquared += halfSize * halfSize;
        }
        stampf(cmodel, 0x18, std::sqrt(radiusSquared));
        const bool hasShape = index > 0 && model.shapeIndex != std::numeric_limits<uint16_t>::max();
        if (hasShape)
        {
            stamp64(cmodel, 0x20, insertedReference ? alias : PTR_INSERT);
            std::memcpy(cmodel + 0x28, &model.shapeIndex, sizeof(model.shapeIndex));
            insertedReference = true;
        }
        else
        {
            const uint16_t shape = index == 0 ? 0 : std::numeric_limits<uint16_t>::max();
            std::memcpy(cmodel + 0x28, &shape, sizeof(shape));
        }
        const uint16_t noNavObstacle = std::numeric_limits<uint16_t>::max();
        std::memcpy(cmodel + 0x2A, &noNavObstacle, sizeof(noNavObstacle));
        zb.write(cmodel, sizeof(cmodel));
    }

    if (insertedReference)
    {
        zb.align(7);
        if (zb.streamSize(XFILE_BLOCK_VIRTUAL) != insertionOffset)
            throw std::runtime_error("Replay cmodel insertion offset drifted");
        zb.reserveCalc(8);
        emitPhysicsAssetReference(zb);
    }
}

inline void emitMapEdgeListBody(ZoneBuffer &zb, const std::vector<havok::LadderFace> &ladders)
{
    if (ladders.empty() || ladders.size() > 512)
        throw std::runtime_error("Replay ladder edge list count is invalid");

    struct Edge
    {
        float endpoint[2][4];
    };
    struct Metadata
    {
        float adjacentNormal;
        uint16_t openAngle;
        uint8_t flags;
        uint8_t pad;
    };
    static_assert(sizeof(Edge) == 32);
    static_assert(sizeof(Metadata) == 8);

    std::vector<Edge> edges;
    std::vector<Metadata> metadata;
    edges.reserve(ladders.size() * 2);
    metadata.reserve(ladders.size() * 2);
    std::array<float, 3> minimum{std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity()};
    std::array<float, 3> maximum{-minimum[0], -minimum[1], -minimum[2]};
    const auto include = [&](const float *point) {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = (std::min)(minimum[axis], point[axis]);
            maximum[axis] = (std::max)(maximum[axis], point[axis]);
        }
    };

    for (const auto &ladder : ladders)
    {
        Edge center{};
        std::copy_n(ladder.top.data(), 3, center.endpoint[0]);
        std::copy_n(ladder.bottom.data(), 3, center.endpoint[1]);
        edges.push_back(center);
        // Replay EdgeAdjacentFaceNormalUnpack (13F04E0) rotates world X
        // around the edge direction. Our centerline runs downwards (-Z),
        // so the reference angle is the negative world yaw. BG_GetLadderInfo
        // (CC4470 -> 13EC520) averages the two adjacent normals; a synthetic
        // centerline has one authored facing, so both normals must coincide.
        metadata.push_back({-std::atan2(ladder.normal[1], ladder.normal[0]), 0, 4, 0});

        Edge width{};
        const float tangent[2]{-ladder.normal[1], ladder.normal[0]};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            width.endpoint[0][axis] = ladder.top[axis];
            width.endpoint[1][axis] = ladder.top[axis];
        }
        width.endpoint[0][0] += tangent[0] * ladder.width * 0.5f;
        width.endpoint[0][1] += tangent[1] * ladder.width * 0.5f;
        width.endpoint[1][0] -= tangent[0] * ladder.width * 0.5f;
        width.endpoint[1][1] -= tangent[1] * ladder.width * 0.5f;
        edges.push_back(width);
        metadata.push_back({0.0f, 15944, 4, 0});
        include(center.endpoint[0]);
        include(center.endpoint[1]);
        include(width.endpoint[0]);
        include(width.endpoint[1]);
    }

    uint8_t body[iw8sz::EDGELIST]{};
    stamp64(body, 0x00, PTR_FOLLOWS);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        stampf(body, 0x08 + axis * 4, minimum[axis]);
        stampf(body, 0x14 + axis * 4, maximum[axis]);
    }
    stamp32(body, 0x24, 4);
    stamp32(body, 0x28, 4);
    stamp32(body, 0x30, static_cast<uint32_t>(edges.size()));
    body[0x34] = 1;
    stamp32(body, 0x38, static_cast<uint32_t>(edges.size()));
    stamp64(body, 0x40, PTR_FOLLOWS);
    stamp64(body, 0x48, PTR_FOLLOWS);
    stamp32(body, 0x50, 1);
    stamp64(body, 0x58, PTR_FOLLOWS);
    stamp32(body, 0x70, static_cast<uint32_t>(edges.size()));
    stamp64(body, 0x78, PTR_FOLLOWS);

    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(body, sizeof(body));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr("mapedgelist_00000");
    zb.align(31); // Replay AllocLoad_float4x2 (0xDF1B70).
    zb.write(edges.data(), edges.size() * sizeof(Edge));
    zb.align(3);
    zb.write(metadata.data(), metadata.size() * sizeof(Metadata));

    uint8_t octree[0x170]{};
    float halfWidth = 0.5f;
    for (std::size_t axis = 0; axis < 3; ++axis)
        halfWidth = (std::max)(halfWidth, (maximum[axis] - minimum[axis]) * 0.5f);
    for (std::size_t level = 0; level < 16; ++level)
    {
        const float levelHalfWidth = std::ldexp(halfWidth, -static_cast<int>(level));
        for (std::size_t axis = 0; axis < 3; ++axis)
            stampf(octree, level * 16 + axis * 4, levelHalfWidth);
        stampf(octree, 0x124 + level * 4, 3.0f * levelHalfWidth * levelHalfWidth);
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
        stampf(octree, 0x100 + axis * 4, minimum[axis]);
    stampf(octree, 0x10C, -halfWidth);
    octree[0x116] = 0;
    octree[0x117] = 6;
    stamp32(octree, 0x118, 0);
    stamp32(octree, 0x11C, static_cast<uint32_t>(edges.size()));
    octree[0x164] = 2;
    octree[0x165] = 3;
    zb.align(15);
    zb.write(octree, sizeof(octree));
    zb.align(3);
    for (uint32_t index = 0; index < edges.size(); ++index)
        zb.writeT(index);
    zb.popStream();
    zb.popStream();
}

// Emit one MapEnts asset body: struct(0x408)->TEMP_PRELOAD(1), then name + entityString ->
// VIRTUAL(8). Caller is responsible for the surrounding push/pop. `ents` is the IW8 numeric-keyId
// entityString (NUL added here).
inline void emitMapEntsBody(ZoneBuffer &zb, const char *assetName, const std::string &ents,
                            const ReplaySpawns *spawns = nullptr,
                            const havok::BakeResult *collision = nullptr,
                            const uint64_t edgeListAlias = PTR_NULL,
                            const uint32_t dynamicModelCount = 0,
                            const uint32_t dynamicBrushCount = 0)
{
    uint8_t me[kSizeMapEnts];
    std::memset(me, 0, sizeof(me));
    stamp64(me, kME_name, PTR_FOLLOWS);
    stamp64(me, kME_entityString, PTR_FOLLOWS);
    stamp32(me, kME_numEntityChars, (uint32_t)(ents.size() + 1)); // strlen + NUL
    if (spawns && !spawns->records.empty())
    {
        stamp32(me, 0x128, static_cast<uint32_t>(spawns->records.size()));
        stamp64(me, 0x130, PTR_FOLLOWS);
    }
    if (collision)
    {
        if (!collision->triggers.empty())
        {
            std::size_t hullCount = 0;
            std::size_t slabCount = 0;
            for (const auto &trigger : collision->triggers)
                for (const auto &hull : collision->hulls)
                    if (hull.model == trigger.collisionModel)
                    {
                        ++hullCount;
                        slabCount += hull.slabs.size();
                    }
            stamp32(me, kME_trigger + 0x00, static_cast<uint32_t>(collision->triggers.size()));
            stamp64(me, kME_trigger + 0x08, PTR_FOLLOWS);
            stamp32(me, kME_trigger + 0x10, static_cast<uint32_t>(hullCount));
            stamp64(me, kME_trigger + 0x18, PTR_FOLLOWS);
            stamp32(me, kME_trigger + 0x20, static_cast<uint32_t>(slabCount));
            stamp64(me, kME_trigger + 0x28, slabCount ? PTR_FOLLOWS : PTR_NULL);
        }
        if (!collision->entities.empty())
        {
            stamp32(me, kME_havokEntsShapeDataSize,
                    static_cast<uint32_t>(collision->entities.size()));
            stamp64(me, kME_havokEntsShapeData, PTR_FOLLOWS);
        }
        if (!collision->models.empty())
        {
            stamp32(me, kME_numSubModels, static_cast<uint32_t>(collision->models.size()));
            stamp64(me, kME_cmodels, PTR_FOLLOWS);
        }
        if (!collision->ladders.empty())
        {
            if (!edgeListAlias)
                throw std::runtime_error("Replay ladder edge list alias is missing");
            stamp32(me, kME_edgeListUsedQueryTypes, 4);
            stamp32(me, kME_numEdgeLists, 1);
            stamp64(me, kME_edgeLists, PTR_FOLLOWS);
            stamp64(me, kME_edgeListSpatialTree, PTR_FOLLOWS);
        }
    }
    const uint64_t dynamicCount = static_cast<uint64_t>(dynamicModelCount) + dynamicBrushCount;
    if (dynamicCount > UINT32_MAX)
        throw std::runtime_error("Replay dynamic-entity count exceeds 32 bits");
    if (dynamicCount)
    {
        stamp32(me, kME_dynEntCount, dynamicModelCount);
        stamp32(me, kME_dynEntCount + sizeof(uint32_t), dynamicBrushCount);
        stamp32(me, kME_dynEntCountTotal, static_cast<uint32_t>(dynamicCount));
        stamp32(me, kME_dynEntityListsCount, 1);
        stamp64(me, kME_dynEntListIds, PTR_FOLLOWS);
        if (dynamicModelCount > UINT16_MAX || dynamicBrushCount > UINT16_MAX)
            throw std::runtime_error("Replay no-spatial dynamic-entity count exceeds 16 bits");
        stamp16(me, kME_dynEntNoSpatialCount, static_cast<uint16_t>(dynamicModelCount));
        stamp16(me, kME_dynEntNoSpatialCount + sizeof(uint16_t),
                static_cast<uint16_t>(dynamicBrushCount));
        stamp16(me, kME_dynEntMaxClientHistoryCount, 10000);
        me[kME_dynEntMaxPosePartCount] = 1;
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
    zb.align(
        7); // struct -> retail STREAM 1: live [STREAM-STATE] map_ents curStream=1 (srv's small TEMP
            // scratch s0=0x6F0 exposes streams 0,1 only -- a real game zone confirms s1 reserves,
            // s2 does NOT). gfx_map/glass_map differ (curStream=2, big scratch) -- do NOT unify.
    zb.write(me, sizeof(me));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // name
    zb.write(ents.data(), ents.size());
    zb.writeT<uint8_t>(0); // entityString + NUL
    if (collision)
        emitMapTriggers(zb, *collision);
    if (spawns)
        spawns->writeRecords(zb);
    if (collision && !collision->entities.empty())
    {
        zb.align(15);
        zb.write(collision->entities.data(), collision->entities.size());
    }
    if (collision)
        emitCmodels(zb, *collision);
    if (collision && !collision->ladders.empty())
    {
        zb.align(7);
        zb.writeT(edgeListAlias);
        zb.align(7);
        uint8_t tree[24]{};
        stamp64(tree, 8, PTR_FOLLOWS);
        stamp32(tree, 20, 1);
        zb.write(tree, sizeof(tree));
        zb.align(3);
        zb.writeT<uint32_t>(0);
    }
    if (dynamicCount)
    {
        zb.align(1);
        zb.writeT<uint16_t>(0); // dynentitylist0
    }
    for (unsigned i = 0; i < 2; ++i)
    {
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
inline void buildMapZone(ZoneBuffer &zb, const char *assetName, const std::string &ents)
{
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

    // 3a) asset[0] map_ents(29): the standalone MapEnts. Keep its native
    // insertion-slot alias for the clipMap dependency below.
    zb.align(7);
    const uint64_t mapEntsInsertionOffset = zb.streamSize(XFILE_BLOCK_VIRTUAL);
    if (mapEntsInsertionOffset >= UINT32_MAX)
        throw std::runtime_error("map_ents insertion offset exceeds Replay's packed range");
    const uint64_t mapEntsAlias =
        (static_cast<uint64_t>(XFILE_BLOCK_VIRTUAL) << 32) | (mapEntsInsertionOffset + 1);
    zb.reserveCalc(8); // native DB_InsertPointer slot
    emitMapEntsBody(zb, assetName, ents);

    // 3b) asset[1] col_map(23): clipMap struct -> TEMP_PRELOAD(1), variable data -> VIRTUAL(8).
    zb.align(7);
    zb.reserveCalc(8); // top-level clipMap DB_InsertPointer slot
    uint8_t cm[kSizeClipMap];
    std::memset(cm, 0, sizeof(cm));
    stamp64(cm, kCM_name, PTR_FOLLOWS);
    stamp32(cm, kCM_isInUse, 1);
    stamp64(cm, kCM_mapEnts, mapEntsAlias);
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
    zb.align(
        7); // struct -> retail STREAM 1: live [STREAM-STATE] map_ents curStream=1 (srv's small TEMP
            // scratch s0=0x6F0 exposes streams 0,1 only -- a real game zone confirms s1 reserves,
            // s2 does NOT). gfx_map/glass_map differ (curStream=2, big scratch) -- do NOT unify.
    zb.write(cm, sizeof(cm));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // clipMap name
    zb.popStream();
    zb.popStream();

    zb.popStream(); // VIRTUAL (XAsset array) -> TEMP
}

// ---------------------------------------------------------------------------------------------------
// Structural Replay srv zone: map_ents(29) + col_map(23) + com_map(24).
// This is the extension of buildMapZone that adds com_map (research doc 10/14b: com_map is NOT
// omittable). Optional `bounds` overrides the generous broadphase AABB
// when the dump's colmap verts gave a tight one. All three structs are zeroed buffers with
// named-field stamps validated against the iw8::maps:: pinned offsets.
struct MapBounds
{
    float mn[3];
    float mx[3];
    bool valid = false;
};
// Replay mp_aniyah_tac ComPrimaryLight[1], including the adjacent .fp patch.
// These are native linear-light values, not display RGB or an exposure scale.
struct MapSun
{
    struct PrimaryLight
    {
        uint8_t type = 0;
        uint8_t exponent = 0;
        bool canUseShadowMap = false;
        float color[3]{};
        float direction[3]{};
        float up[3]{};
        float origin[3]{};
        float radius = 0.0f;
        float cosHalfFovOuter = 0.0f;
        float cosHalfFovInner = 0.0f;
        float rotationLimit = 0.0f;
        float translationLimit = 0.0f;
        std::string definition;
    };

    float intensity = 24.649917602539062f;
    float color[3]{0.9941421151161194f, 1.0032469034194946f, 0.9850855469703674f};
    float direction[3]{-0.5795004963874817f, 0.4210316836833954f, 0.6977904438972473f};
    float up[3]{};
    uint32_t sunPrimaryLightIndex = 1;
    std::vector<PrimaryLight> primaryLights;
};

// Emit one ComWorld asset body (com_map 24): struct(0xA8)->TEMP_PRELOAD(1),
// variable data -> VIRTUAL(8). The policy fields match a stock 1.20 Replay MP
// world. The authored map still owns its sun direction, color and intensity.
inline void emitComWorldBody(ZoneBuffer &zb, const char *assetName, const MapSun &lighting = {})
{
    const uint32_t primaryLightCount =
        lighting.primaryLights.empty() ? 2u : static_cast<uint32_t>(lighting.primaryLights.size());
    if (primaryLightCount < 2 || lighting.sunPrimaryLightIndex >= primaryLightCount)
        throw std::runtime_error("invalid primary-light table");

    uint8_t cw[kSizeComWorld];
    std::memset(cw, 0, sizeof(cw));
    stamp64(cw, kCW_name, PTR_FOLLOWS);
    stamp32(cw, kCW_isInUse, 1);
    stamp32(cw, 0x0C, 1); // useForwardPlus
    stamp32(cw, 0x10, 3); // production bake quality
    constexpr float trVisRadii[]{4500.0f, 25000.0f, 30000.0f, 40000.0f, 50000.0f};
    for (unsigned i = 0; i < 5; ++i)
        stampf(cw, 0x18 + i * 4, trVisRadii[i]);
    stampf(cw, 0x2C, 1500.0f);
    // Primary light zero is the reserved unlit entry. Replay's light-copy caller
    // passes [0, count-1] without a zero-count guard (18D9690).
    stamp32(cw, kCW_primaryLightCount, primaryLightCount);
    stamp64(cw, 0x38, PTR_FOLLOWS);
    stamp32(cw, 0x44, primaryLightCount); // firstScriptablePrimaryLight
    stamp32(cw, 0x48, 1);                 // one resident transient-table entry
    stamp64(cw, 0x50, PTR_FOLLOWS);
    std::memset(cw + 0x78, 0xFF, 0x30); // stock initial state for an empty Umbra gate set
    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(
        7); // struct -> retail STREAM 1: live [STREAM-STATE] map_ents curStream=1 (srv's small TEMP
            // scratch s0=0x6F0 exposes streams 0,1 only -- a real game zone confirms s1 reserves,
            // s2 does NOT). gfx_map/glass_map differ (curStream=2, big scratch) -- do NOT unify.
    zb.write(cw, sizeof(cw));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // name
    zb.align(7);
    for (uint32_t index = 0; index < primaryLightCount; ++index)
    {
        uint8_t light[0xA0]{}; // Replay 1.20 Load_ComWorld DF27B0
        const MapSun::PrimaryLight *source =
            lighting.primaryLights.empty() ? nullptr : &lighting.primaryLights[index];
        if (source)
        {
            light[1] = source->type;
            light[2] = source->canUseShadowMap ? 1 : 0;
            stampf(light, 0x10, index == 0 ? 0.0f : 1.0f);
            for (unsigned component = 0; component < 3; ++component)
            {
                stampf(light, 0x20 + 4 * component, source->color[component]);
                stampf(light, 0x2C + 4 * component, source->direction[component]);
                stampf(light, 0x38 + 4 * component, source->up[component]);
                stampf(light, 0x44 + 4 * component, source->origin[component]);
            }
            stampf(light, 0x50, source->radius);
            stampf(light, 0x6C, source->cosHalfFovOuter);
            stampf(light, 0x70, source->cosHalfFovInner);
            stampf(light, 0x80, source->type >= 2 ? 0.0018f : 0.0f);
            stampf(light, 0x84, source->type >= 2 ? 0.2f : 0.0f);
            stampf(light, 0x8C, source->rotationLimit);
            stampf(light, 0x90, source->translationLimit);
            stamp64(light, 0x98, source->definition.empty() ? PTR_NULL : PTR_FOLLOWS);
        }
        if (index == lighting.sunPrimaryLightIndex)
        {
            light[1] = 1;
            stampf(light, 0x10, lighting.intensity);
            for (unsigned component = 0; component < 3; ++component)
            {
                stampf(light, 0x20 + 4 * component, lighting.color[component]);
                stampf(light, 0x2C + 4 * component, lighting.direction[component]);
                stampf(light, 0x38 + 4 * component, lighting.up[component]);
            }
            stamp64(light, 0x98, PTR_NULL);
        }
        zb.write(light, sizeof(light));
    }
    if (!lighting.primaryLights.empty())
        for (const auto &light : lighting.primaryLights)
            if (!light.definition.empty())
                zb.writeStr(light.definition.c_str());
    zb.align(1);
    zb.writeT<uint16_t>(0x8000); // stock resident transient-table entry
    zb.popStream();
    zb.popStream();
}

// Required level NCS assets. The converter has no converted per-level asset names,
// so entryCount/stringList are zero. Native ProcessNetConstStringMap accepts this.
inline void emitLevelNetConstStrings(ZoneBuffer &zb, unsigned type)
{
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

inline void buildSrvMapZone(ZoneBuffer &zb, const char *assetName, const std::string &ents,
                            const MapBounds &bounds, const MapSun &lighting,
                            const havok::BakeResult &collision, const uint32_t glassInitCount,
                            const uint32_t glassPieceLimit, const uint32_t dynamicModelCount = 0,
                            const uint32_t dynamicBrushCount = 0)
{
    ReplaySpawns spawns(ents);
    const bool hasLadders = !collision.ladders.empty();
    bool hasLinearLightDef = false;
    for (size_t index = 0; index < lighting.primaryLights.size(); ++index)
        if (index != lighting.sunPrimaryLightIndex &&
            lighting.primaryLights[index].definition == "light_point_linear")
            hasLinearLightDef = true;
    uint32_t lightDefNameStringIndex = 0;
    if (hasLinearLightDef)
    {
        if (spawns.records.empty())
            throw std::runtime_error("local lightdef requires Replay spawn string list");
        lightDefNameStringIndex = spawns.intern("light_point_linear");
    }
    // 1) XAssetList root -> TEMP(0).
    zb.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = spawns.records.empty() ? 0 : static_cast<uint32_t>(spawns.strings.size());
    al.stringList.loaded = 0;
    al.stringList.strings = al.stringList.count ? PTR_FOLLOWS : PTR_NULL;
    al.assetCount = 3 + replayncs::Count + (hasLadders ? 1u : 0u) +
                    (hasLinearLightDef ? 1u : 0u);
    al.assetReadPos = 0;
    al.assets = PTR_FOLLOWS;
    zb.writeT(al);
    zb.popStream();

    // 2) Top-level assets -> VIRTUAL. The edge list precedes MapEnts so its packed alias is
    // available when MapEnts serializes edgeLists.
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    uint64_t lightDefNameAlias = PTR_NULL;
    if (hasLinearLightDef)
    {
        const uint64_t start = (zb.streamSize(XFILE_BLOCK_VIRTUAL) + 7) & ~uint64_t(7);
        uint64_t offset = start + spawns.strings.size() * sizeof(uint64_t);
        for (uint32_t index = 1; index < lightDefNameStringIndex; ++index)
            offset += spawns.strings[index].size() + 1;
        if (offset >= UINT32_MAX)
            throw std::runtime_error("lightdef name offset exceeds Replay's packed range");
        lightDefNameAlias = (static_cast<uint64_t>(XFILE_BLOCK_VIRTUAL) << 32) | (offset + 1);
    }
    spawns.writeStrings(zb);
    zb.align(7);
    if (hasLadders)
    {
        IW8_XAsset edgeList{};
        edgeList.type = ASSET_TYPE_EDGELIST;
        edgeList.header = PTR_INSERT;
        zb.writeT(edgeList);
    }
    IW8_XAsset a0{};
    a0.type = ASSET_TYPE_MAP_ENTS;
    a0.header = PTR_INSERT;
    zb.writeT(a0);
    IW8_XAsset a1{};
    a1.type = ASSET_TYPE_COL_MAP;
    a1.header = PTR_INSERT;
    zb.writeT(a1);
    if (hasLinearLightDef)
    {
        IW8_XAsset lightDef{};
        lightDef.type = ASSET_TYPE_LIGHTDEF;
        lightDef.header = PTR_FOLLOWS;
        zb.writeT(lightDef);
    }
    IW8_XAsset a2{};
    a2.type = ASSET_TYPE_COM_MAP;
    a2.header = PTR_INSERT;
    zb.writeT(a2);
    for (unsigned type = 0; type < replayncs::Count; ++type)
    {
        IW8_XAsset asset{};
        asset.type = ASSET_TYPE_NET_CONST_STRINGS;
        asset.header = PTR_INSERT;
        zb.writeT(asset);
    }

    uint64_t edgeListAlias = PTR_NULL;
    if (hasLadders)
    {
        zb.align(7);
        const uint64_t edgeListInsertionOffset = zb.streamSize(XFILE_BLOCK_VIRTUAL);
        if (edgeListInsertionOffset >= UINT32_MAX)
            throw std::runtime_error("edgelist insertion offset exceeds Replay's packed range");
        edgeListAlias =
            (static_cast<uint64_t>(XFILE_BLOCK_VIRTUAL) << 32) | (edgeListInsertionOffset + 1);
        zb.reserveCalc(8);
        emitMapEdgeListBody(zb, collision.ladders);
    }

    // 3a) map_ents(29). Record the packed pointer to the top-level asset's
    // DB_InsertPointer slot so clipMap_t can reference this same asset.
    zb.align(7);
    const uint64_t mapEntsInsertionOffset = zb.streamSize(XFILE_BLOCK_VIRTUAL);
    if (mapEntsInsertionOffset >= UINT32_MAX)
        throw std::runtime_error("map_ents insertion offset exceeds Replay's packed range");
    const uint64_t mapEntsAlias =
        (static_cast<uint64_t>(XFILE_BLOCK_VIRTUAL) << 32) | (mapEntsInsertionOffset + 1);
    zb.reserveCalc(8); // native DB_InsertPointer slot
    emitMapEntsBody(zb, assetName, ents, &spawns, &collision, edgeListAlias, dynamicModelCount,
                    dynamicBrushCount);

    // 3b) col_map(23): clipMap struct -> TEMP_PRELOAD, variable data -> VIRTUAL.
    // mapEnts is a packed reference to asset[0], matching stock zonetool output;
    // it must not be loaded as a second inline asset.
    zb.align(7);
    zb.reserveCalc(8); // top-level clipMap DB_InsertPointer slot
    uint8_t cm[kSizeClipMap];
    std::memset(cm, 0, sizeof(cm));
    stamp64(cm, kCM_name, PTR_FOLLOWS);
    stamp32(cm, kCM_isInUse, 1);
    stamp64(cm, kCM_mapEnts, mapEntsAlias);
    stamp64(cm, 0x20, PTR_FOLLOWS);
    cm[0x28] = 1; // R_UpdateActiveStage 19864E0 always copies stage zero.
    // broadphase AABB: tight from the dump's colmap verts when available, else proven generous box.
    if (bounds.valid)
    {
        for (int k = 0; k < 3; ++k)
        {
            stampf(cm, kCM_bpMin + (size_t)k * 4, bounds.mn[k]);
            stampf(cm, kCM_bpMax + (size_t)k * 4, bounds.mx[k]);
        }
    }
    else
    {
        stampf(cm, kCM_bpMin + 0, -100000.f);
        stampf(cm, kCM_bpMin + 4, -100000.f);
        stampf(cm, kCM_bpMin + 8, -100000.f);
        stampf(cm, kCM_bpMax + 0, 100000.f);
        stampf(cm, kCM_bpMax + 4, 100000.f);
        stampf(cm, kCM_bpMax + 8, 100000.f);
    }
    stamp32(cm, kCM_havokSize, static_cast<uint32_t>(collision.world.size()));
    stamp64(cm, kCM_havokData, collision.world.empty() ? PTR_NULL : PTR_FOLLOWS);
    stamp32(cm, kCM_glassInitCount, glassInitCount);
    stamp32(cm, kCM_glassPieceLimit, glassPieceLimit);
    stamp32(cm, kCM_checksum, 0);

    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(
        7); // struct -> retail STREAM 1: live [STREAM-STATE] map_ents curStream=1 (srv's small TEMP
            // scratch s0=0x6F0 exposes streams 0,1 only -- a real game zone confirms s1 reserves,
            // s2 does NOT). gfx_map/glass_map differ (curStream=2, big scratch) -- do NOT unify.
    zb.write(cm, sizeof(cm));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr(assetName); // clipMap name
    zb.popStream();
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.align(7);
    // Load_clipMap_t E0FD30 -> Load_StageArray E05480, 40 bytes.
    uint8_t stage[40]{};
    stamp64(stage, 0, PTR_FOLLOWS);
    stage[0x16] = 1;
    zb.write(stage, sizeof(stage));
    zb.writeStr("default");
    if (!collision.world.empty())
    {
        zb.align(15);
        zb.write(collision.world.data(), collision.world.size());
    }
    zb.popStream();
    zb.popStream();

    // Shipped Replay Rust carries this definition beside its ComWorld. The
    // target type-34 loader reads a 0x20 body in stream 1 and its name in 5.
    if (hasLinearLightDef)
    {
        // Rust's same-zone LightDef uses PTR_FOLLOWS at the top level and a
        // packed alias for a name string consumed earlier, with no insert slot.
        uint8_t lightDef[iw8sz::LIGHTDEF]{};
        stamp64(lightDef, 0x00, lightDefNameAlias);
        zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        zb.align(7);
        zb.write(lightDef, sizeof(lightDef));
        zb.popStream();
    }

    // 3c) com_map(24): ComWorld valid-empty
    zb.align(7);
    zb.reserveCalc(8); // top-level ComWorld DB_InsertPointer slot
    emitComWorldBody(zb, assetName, lighting);

    for (unsigned type = 0; type < replayncs::Count; ++type)
        emitLevelNetConstStrings(zb, type);

    zb.popStream(); // VIRTUAL (XAsset array) -> TEMP
}

} // namespace iw8
