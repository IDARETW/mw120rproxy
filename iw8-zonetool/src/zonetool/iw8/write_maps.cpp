#include "iw8_structs.h"
#include "iw8_zone.h"
#include "maps_write.h"
#include "replay_lightgrid.h"
#include "replay_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
namespace iw8maps
{

using iw8::ZoneWriter;
using namespace iw8; // XFILE_BLOCK_*, PTR_*

// stamp helpers into a fixed-size zeroed struct buffer
static inline void stamp64(uint8_t *p, size_t off, uint64_t v)
{
    std::memcpy(p + off, &v, 8);
}
static inline void stamp32(uint8_t *p, size_t off, uint32_t v)
{
    std::memcpy(p + off, &v, 4);
}
static inline void stamp16(uint8_t *p, size_t off, uint16_t v)
{
    std::memcpy(p + off, &v, 2);
}
static inline void stampf(uint8_t *p, size_t off, float v)
{
    std::memcpy(p + off, &v, 4);
}

void emitFxMapBody(ZoneWriter &zw, const char *assetName, const replayrender::Mesh &mesh)
{
    const uint32_t pieceCount = static_cast<uint32_t>(mesh.glassPanes.size());
    if (!pieceCount)
        return;

    std::vector<std::array<uint8_t, 120>> definitions;
    std::vector<uint8_t> definitionIndices;
    for (const auto &pane : mesh.glassPanes)
    {
        std::array<uint8_t, 120> definition{};
        stampf(definition.data(), 0, pane.halfThickness);
        std::memcpy(definition.data() + 4, pane.texVecs.data(), sizeof(pane.texVecs));
        stamp32(definition.data(), 0x14, UINT32_MAX);
        const auto material = zw.assetAlias(ASSET_TYPE_MATERIAL, pane.material);
        stamp64(definition.data(), 0x18, material);
        stamp64(definition.data(), 0x20, material);
        stamp64(definition.data(), 0x28,
                zw.assetAlias(ASSET_TYPE_PHYSICSASSET, kGlassPhysicsName));
        for (const size_t offset : {0x30u, 0x38u, 0x40u})
            stamp64(definition.data(), offset, PTR_FOLLOWS);
        stampf(definition.data(), 0x68, 1.0f / (128.0f * 128.0f));
        stampf(definition.data(), 0x6C, 0.6341288f);
        definition[0x70] = 0xFF;
        const auto found = std::find(definitions.begin(), definitions.end(), definition);
        const auto index = static_cast<size_t>(found - definitions.begin());
        if (found == definitions.end())
        {
            if (definitions.size() == 256)
                throw std::runtime_error("Replay glass exceeds 256 distinct pane definitions");
            definitions.push_back(definition);
        }
        definitionIndices.push_back(static_cast<uint8_t>(index));
    }

    constexpr uint32_t sparePieces = 512;
    const uint32_t pieceLimit = pieceCount + sparePieces;
    const uint32_t pieceWordCount = (pieceLimit + 31u) >> 5;
    const uint32_t cellCount = static_cast<uint32_t>(mesh.cells.size());
    const uint32_t geoDataLimit = pieceLimit * 8;
    const uint32_t initGeoDataCount = pieceCount * 4;
    constexpr size_t globals = 0x08;
    constexpr size_t system = 0x30;

    std::vector<uint8_t> world(iw8sz::FX_MAP, 0);
    stamp64(world.data(), 0x00, PTR_FOLLOWS);
    stamp32(world.data(), globals + 0x00, static_cast<uint32_t>(definitions.size()));
    stamp64(world.data(), globals + 0x08, PTR_FOLLOWS); // defs
    stamp64(world.data(), globals + 0x20, PTR_FOLLOWS); // runtime distances
    stamp32(world.data(), system + 0x08, pieceLimit);
    stamp32(world.data(), system + 0x0C, pieceWordCount);
    stamp32(world.data(), system + 0x10, cellCount);
    stamp32(world.data(), system + 0x18, std::numeric_limits<uint16_t>::max());
    stamp32(world.data(), system + 0x1C, geoDataLimit);
    stamp32(world.data(), system + 0x24, initGeoDataCount);
    for (const size_t offset : {0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x88})
        stamp64(world.data(), system + offset, PTR_FOLLOWS);
    stamp32(world.data(), system + 0x84, pieceCount);

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);
    zw.write(world.data(), world.size());
    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    zw.writeStr(assetName);

    zw.align(7);
    constexpr std::array<std::string_view, 3> glassEffects{
        "vfx/code/glass/glass_shatter_piece",
        "vfx/code/glass/glass_shatter_64x64",
        "vfx/code/glass/glass_shatter_32x32"};
    zw.write(definitions.data(), definitions.size() * sizeof(definitions.front()));
    for (size_t index = 0; index < definitions.size(); ++index)
    {
        for (const auto name : glassEffects)
        {
            // Replay FxGlassDef loads each non-null VFX reference as a 128-byte
            // TEMP_PRELOAD alias, followed by its comma-prefixed virtual name.
            zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
            zw.align(15);
            std::array<uint8_t, 128> particle{};
            stamp64(particle.data(), 0, PTR_FOLLOWS);
            zw.write(particle.data(), particle.size());
            zw.pushStream(XFILE_BLOCK_VIRTUAL);
            zw.writeStr("," + std::string(name));
            zw.popStream();
            zw.popStream();
        }
    }

    zw.align(1);
    for (const auto &pane : mesh.glassPanes)
    {
        const int16_t width = static_cast<int16_t>(std::lround(pane.halfWidth * 32.0f));
        const int16_t height = static_cast<int16_t>(std::lround(pane.halfHeight * 32.0f));
        const int16_t vertices[4][2]{{width, static_cast<int16_t>(-height)},
                                     {static_cast<int16_t>(-width), static_cast<int16_t>(-height)},
                                     {static_cast<int16_t>(-width), height},
                                     {width, height}};
        zw.write(vertices, sizeof(vertices));
    }

    zw.align(3);
    for (size_t index = 0; index < mesh.glassPanes.size(); ++index)
    {
        const auto &pane = mesh.glassPanes[index];
        uint8_t state[52]{};
        std::memcpy(state, pane.quaternion.data(), sizeof(pane.quaternion));
        std::memcpy(state + 0x10, pane.origin.data(), sizeof(pane.origin));
        stampf(state, 0x1C,
               std::sqrt(pane.halfWidth * pane.halfWidth + pane.halfHeight * pane.halfHeight));
        std::memcpy(state + 0x20, pane.texCoordOrigin.data(), sizeof(pane.texCoordOrigin));
        stamp32(state, 0x28, 0xF0000000u);
        stampf(state, 0x2C, pane.halfWidth * pane.halfHeight * 8.0f);
        state[0x30] = definitionIndices[index];
        state[0x31] = 4;
        zw.write(state, sizeof(state));
    }
    zw.popStream();
    zw.popStream();

    // Replay creates mutable fracture data in its zero-filled stream 4.
    zw.pushStream(XFILE_BLOCK_SHARED_STREAM);
    zw.align(3);
    zw.reserveCalc(definitions.size() * sizeof(float)); // FxGlassGlobals::distances[defCount]
    zw.align(3);
    zw.reserveCalc(size_t(pieceLimit) * 32); // piecePlaces
    zw.align(3);
    zw.reserveCalc(size_t(pieceLimit) * 32); // pieceStates
    zw.align(3);
    zw.reserveCalc(size_t(pieceLimit) * 40); // pieceDynamics
    zw.align(1);
    zw.reserveCalc(size_t(geoDataLimit) * 4);
    zw.align(3);
    zw.reserveCalc(size_t(pieceWordCount) * 4);
    zw.align(3);
    zw.reserveCalc(size_t(cellCount) * pieceWordCount * 4);
    zw.reserveCalc((size_t(pieceLimit) + 15) & ~size_t(15));
    zw.align(3);
    zw.reserveCalc(size_t(pieceLimit) * 12);
    zw.align(3);
    zw.reserveCalc(size_t((pieceLimit + 3u) & ~3u) * 4);
    zw.popStream();

    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zw.align(7);
    zw.reserveCalc(iw8sz::FX_MAP);
    zw.align(7);
    for (size_t index = 0; index < glassEffects.size() * definitions.size(); ++index)
    {
        zw.align(15);
        zw.reserveCalc(128);
    }
    zw.align(31);
    zw.popStream();
}

void emitGlassMapBody(ZoneWriter &zw, const char *assetName, const replayrender::Mesh &mesh)
{
    const uint32_t pieceCount = static_cast<uint32_t>(mesh.glassPanes.size());
    std::vector<uint8_t> gl(kSizeGlass, 0);
    stamp64(gl.data(), kGL_name, PTR_FOLLOWS);
    stamp64(gl.data(), kGL_glassData, PTR_FOLLOWS);
    // Replay Load_GlassWorldPtr uses stream 1 for the body.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);
    zw.write(gl.data(), gl.size());
    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    zw.writeStr(assetName); // name
    // Replay Load_GlassWorld DF31E0 calls AllocLoad_G_GlassData DFC9E0
    // (alignment 7), then Load_G_GlassData DFD220 (40 bytes). G_InitGlass
    // 1213500 always dereferences this object even with no breakable pieces.
    zw.align(7);
    uint8_t glassData[40]{};
    if (pieceCount)
    {
        stamp64(glassData, 0x00, PTR_FOLLOWS);
        stamp32(glassData, 0x08, pieceCount);
        stamp64(glassData, 0x20, PTR_FOLLOWS);
    }
    zw.write(glassData, sizeof(glassData));
    if (pieceCount)
    {
        zw.align(3);
        std::vector<uint8_t> pieces(size_t(pieceCount) * 12);
        zw.write(pieces.data(), pieces.size());

        zw.align(7);
        uint8_t tree[24]{};
        stamp64(tree, 0x08, PTR_FOLLOWS);
        stamp32(tree, 0x14, pieceCount);
        zw.write(tree, sizeof(tree));
        zw.align(3);
        for (uint32_t piece = 0; piece < pieceCount; ++piece)
            zw.writeT(piece);
    }
    zw.popStream();
    zw.popStream();
    // Preload/Postload use stream 2 (Load uses stream 1) for the same body.
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zw.align(7);
    zw.reserveCalc(kSizeGlass);
    zw.align(31);
    zw.popStream();
}

// Replay render world with converted BSP triangles, authored brush models, and a sun.
void emitGfxMapBody(ZoneWriter &zw, const char *assetName, const std::string &meshPath,
                    const replayrender::Mesh &mesh,
                    const uint32_t primaryLightCount, const uint32_t sunPrimaryLightIndex,
                    const replayrender::StaticModels &staticModels,
                    const uint32_t dynamicModelCount, const uint32_t dynamicBrushCount)
{
    if (primaryLightCount < 2 || sunPrimaryLightIndex >= primaryLightCount)
        throw std::runtime_error("invalid primary-light table for GfxWorld");

    const auto lightGrid = replaylightgrid::Load(meshPath);
    const auto umbraTome = replayrender::BuildUmbraTome(mesh, staticModels);
    const auto cellCount = static_cast<uint32_t>(mesh.cells.size());
    const auto cellWordCount = (cellCount + 31u) >> 5;
    std::vector<uint8_t> gw(kSizeGfxWorld, 0);
    stamp64(gw.data(), kGW_name, PTR_FOLLOWS);
    stamp64(gw.data(), kGW_baseName, PTR_FOLLOWS);
    stamp32(gw.data(), kGW_bspVersion, 243);
    stamp32(gw.data(), 0x14, sunPrimaryLightIndex); // lastSunPrimaryLightIndex
    stamp32(gw.data(), 0x18, primaryLightCount);
    // Empty mutable/scriptable/moving ranges begin immediately after the two
    // primary lights. Stock Replay worlds use these end indices even when all
    // corresponding counts are zero.
    stamp32(gw.data(), 0x1C, primaryLightCount); // firstMutablePrimaryLight
    stamp32(gw.data(), 0x24, primaryLightCount); // firstStaticScriptablePrimaryLight
    stamp32(gw.data(), 0x2C, primaryLightCount); // firstScriptablePrimaryLight
    stamp32(gw.data(), 0x34, primaryLightCount); // firstMovingScriptablePrimaryLight
    stamp64(gw.data(), 0x3D68, PTR_FOLLOWS);     // runtime GfxLight[], zero-fill stream 4
    // Native Load_GfxBrushModelArray DD1750 consumes 96 bytes per model;
    // allocator DCF750 uses alignment mask 3. Entity model indices refer to
    // this array directly, so preserve every authored IW3 brush-model slot.
    stamp32(gw.data(), 0x3DF0, static_cast<uint32_t>(mesh.brushModels.size()));
    stamp64(gw.data(), 0x3DF8, PTR_FOLLOWS);
    // Stock Replay inflates the world model bounds by one unit. The synthetic
    // sky cube is a background surface, not the physical extent of the world.
    mesh.sceneBounds.Write(gw.data() + kGW_bounds, mesh.count ? 1.f : 0.f);
    stamp32(gw.data(), kGW_dpvsPlanes + kGW_dpp_cellCount, cellCount);
    stamp16(gw.data(), kGW_dpvsPlanes + kGW_dpp_planeCount,
            static_cast<uint16_t>(mesh.planes.size()));
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_planes,
            mesh.planes.empty() ? PTR_NULL : PTR_FOLLOWS);
    stamp16(gw.data(), kGW_dpvsPlanes + kGW_dpp_nodeCount,
            static_cast<uint16_t>(mesh.nodes.size()));
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_nodes, PTR_FOLLOWS); // REQUIRED non-null
    // Load_GfxWorldDpvsPlanes D94FB0 allocates 512 uints per cell in stream 4.
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_sceneEntCellBits, PTR_FOLLOWS);
    stamp64(gw.data(), kGW_cells, PTR_FOLLOWS); // REQUIRED non-null
    stamp64(gw.data(), kGW_cellTransientInfos, PTR_FOLLOWS);
    // Replay draw @0x648: transientZoneCount @+0x184, pointers @+0x188.
    // Zone zero is resident and required by R_ReflectionProbe_WorldStartup.
    stamp32(gw.data(), 0x648 + 0x184, 1);
    stamp64(gw.data(), 0x648 + 0x188, PTR_FOLLOWS);
    // Replay R_GpuLightGrid_DataAvailable (188E940) checks this byte before
    // sampling. A resident grid uses SINGLE, as in the shipped Shipment world.
    gw[0x7C0] = lightGrid ? 1 : 0;
    // R_SetLightScaleInfo (197CB93) multiplies secondary diffuse by this value.
    // Zero-initializing it disables indirect light, even with a valid grid.
    stampf(gw.data(), 0x37F0, 1.0f);
    stamp64(gw.data(), 0x648 + 0xF0, PTR_FOLLOWS); // mandatory IES lookup image
    stamp64(gw.data(), kGW_cellVisBits,
            PTR_FOLLOWS); // REQUIRED non-null (cellCount-gated memset, no own count)
    stamp64(gw.data(), 0x3EF8, PTR_FOLLOWS); // cellHasSunLitSurfsBits[cellWordCount]
    const uint32_t primaryLightVisDataCount = (primaryLightCount + 31u) >> 5;
    stamp32(gw.data(), 0x3F98 + 8, primaryLightVisDataCount);
    stamp64(gw.data(), 0x41C0, PTR_FOLLOWS);
    // R_EntityMoved 1959A60 indexes localClient*80 + entityNum/32.
    stamp32(gw.data(), 0x3F20, 160);
    stamp64(gw.data(), 0x3F28, PTR_FOLLOWS);
    // Replay's GfxWorld DPVS-dynamic data is separate from the DynEntityList
    // transient population. Each client basis has two local-client bitsets.
    // A nonzero MapEnts/DynEntityList count requires matching resident cell
    // and view bitsets: the native entity update at 1975650 dereferences them.
    const std::array<uint32_t, 2> dynEntClientCounts{dynamicModelCount, dynamicBrushCount};
    const std::array<uint32_t, 2> dynEntWordCounts{
        2u * ((dynamicModelCount + 31u) >> 5),
        2u * ((dynamicBrushCount + 31u) >> 5)};
    for (size_t basis = 0; basis < dynEntClientCounts.size(); ++basis)
    {
        stamp32(gw.data(), replaymap::DynEntDpvs + basis * 4, dynEntWordCounts[basis]);
        stamp32(gw.data(), replaymap::DynEntDpvs + 8 + basis * 4, dynEntClientCounts[basis]);
        stamp32(gw.data(), replaymap::DynEntMotionBitsEntries + basis * 4,
                dynEntWordCounts[basis]);
        if (dynEntWordCounts[basis])
        {
            stamp64(gw.data(), replaymap::DynEntDpvsCellBits + basis * 8, PTR_FOLLOWS);
            stamp64(gw.data(), replaymap::DynEntMotionBits + basis * 8, PTR_FOLLOWS);
            stamp64(gw.data(), replaymap::SceneDynModel + basis * 8, PTR_FOLLOWS);
        }
    }
    // The signed Shipment world has these eleven present view slots for each
    // nonempty basis; their payloads are mutable zero-filled stream-4 bitsets.
    constexpr std::array<uint8_t, 11> dynEntVisViews{0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
    for (const auto view : dynEntVisViews)
    {
        if (dynEntWordCounts[0])
            stamp64(gw.data(), replaymap::DynEntDpvsVisModel + size_t(view) * 8,
                    PTR_FOLLOWS);
        if (dynEntWordCounts[1])
            stamp64(gw.data(), replaymap::DynEntDpvsVisBrush + size_t(view) * 8,
                    PTR_FOLLOWS);
    }
    stamp32(gw.data(), replaymap::UmbraTomeSize, static_cast<uint32_t>(umbraTome.size()));
    stamp64(gw.data(), replaymap::UmbraTomeData, PTR_FOLLOWS);
    replayrender::StampWorld(gw, mesh);
    replayrender::StampStaticModels(gw, staticModels);
    replayrender::StampReflectionProbes(gw, mesh, zw);
    // Replay Load_GfxWorldPtr RVA 0xD96DA0: stream 1, alignment mask 15.
    // The generated tome gives Replay's stock camera query a conservative cell
    // containing every converted world surface. Other unimplemented render data
    // remains null; do not stamp pointers from another build.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(15);
    zw.write(gw.data(), gw.size());
    zw.pushStream(XFILE_BLOCK_VIRTUAL); // dev DB_PushStreamPos(8) -> retail VIRTUAL/5
    zw.writeStr(
        assetName); // name     = raw "maps/mp/mp_test.d3dbsp\0" (struct stamp = PTR_FOLLOWS)
    zw.writeStr(assetName); // baseName = raw "maps/mp/mp_test.d3dbsp\0" (own copy; struct stamp =
                            // PTR_FOLLOWS)
    if (!mesh.planes.empty())
    {
        zw.align(3);
        for (const auto &source : mesh.planes)
        {
            uint8_t plane[20]{};
            std::memcpy(plane, source.normal.data(), sizeof(float) * source.normal.size());
            stampf(plane, 0xC, source.distance);
            plane[0x10] = source.type;
            zw.write(plane, sizeof(plane));
        }
    }
    zw.align(1);
    zw.write(mesh.nodes.data(), mesh.nodes.size() * sizeof(mesh.nodes.front()));
    zw.align(1);
    for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex)
        zw.writeT<uint32_t>(cellIndex); // aabbTreeIndex, resident transient zone zero
    zw.align(7);
    for (const auto &source : mesh.cells)
    {
        std::vector<uint8_t> cell(kSizeGfxCell, 0);
        source.bounds.Write(cell.data());
        stamp16(cell.data(), kGC_portalCount, static_cast<uint16_t>(source.portals.size()));
        stamp64(cell.data(), kGC_portals, source.portals.empty() ? PTR_NULL : PTR_FOLLOWS);
        zw.write(cell.data(), cell.size());
    }
    for (const auto &cell : mesh.cells)
    {
        if (cell.portals.empty())
            continue;
        zw.align(7);
        for (const auto &source : cell.portals)
        {
            uint8_t portal[0x50]{};
            std::memcpy(portal + 0x18, source.plane.data(), sizeof(float) * source.plane.size());
            stamp64(portal, 0x28, PTR_FOLLOWS);
            stamp16(portal, 0x30, static_cast<uint16_t>(source.cell));
            portal[0x34] = static_cast<uint8_t>(source.vertices.size());
            std::memcpy(portal + 0x38, source.hullAxis.data(), sizeof(source.hullAxis));
            zw.write(portal, sizeof(portal));
        }
        for (const auto &portal : cell.portals)
        {
            zw.align(3);
            zw.write(portal.vertices.data(),
                     portal.vertices.size() * sizeof(portal.vertices.front()));
        }
    }
    replayrender::EmitSurfaces(zw, mesh);
    replayrender::EmitStaticModels(zw, staticModels);
    replayrender::EmitReflectionProbes(zw, mesh);
    // Load_GfxWorldDraw D96710 loads iesLookupTexture before transient zones.
    // With no authored local lights, native white gives a neutral IES lookup.
    // Reference the existing image; no pixels or stock image data are copied.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(15);
    uint8_t iesImage[0xE8]{};
    stamp64(iesImage, 0, PTR_FOLLOWS);
    zw.write(iesImage, sizeof(iesImage));
    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    zw.writeStr(",$white");
    zw.popStream();
    zw.popStream();
    // Load_GfxWorldDraw D96710 visits its 1536 inline asset pointers.
    // Load_GfxWorldTransientZonePtr D970B0: stream 1, alignment 7;
    // Load_GfxWorldTransientZone D96E90: 0x148 bytes, name in VIRTUAL.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);
    uint8_t transient[0x148]{};
    stamp64(transient, 0, PTR_FOLLOWS);
    stamp32(transient, 0xD8, cellCount);   // every source cell is resident
    stamp64(transient, 0xE0, PTR_FOLLOWS); // aabbTreeCounts
    stamp64(transient, 0xE8, PTR_FOLLOWS); // one GfxCellTree pointer per cell
    if (lightGrid)
        stamp64(transient, 0xF8, PTR_FOLLOWS);
    replayrender::StampTransient(transient, mesh);
    zw.write(transient, sizeof(transient));
    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    zw.writeStr(assetName);
    replayrender::EmitVertices(zw, mesh);
    zw.align(3);
    for (const auto &cell : mesh.cells)
        zw.writeT<uint32_t>(static_cast<uint32_t>(cell.trees.size()));
    zw.align(7);
    for (const auto &cell : mesh.cells)
        zw.writeT<uint64_t>(cell.trees.empty() ? PTR_NULL : PTR_FOLLOWS);
    for (const auto &cell : mesh.cells)
    {
        if (cell.trees.empty())
            continue;
        // Load_GfxCellTree reads its array count from Replay's temp stream.
        zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        zw.align(3);
        zw.writeT<uint32_t>(static_cast<uint32_t>(cell.trees.size()));
        zw.popStream();
        zw.align(7);
        for (const auto &source : cell.trees)
        {
            if (source.staticModelIndexes.size() > UINT16_MAX)
                throw std::runtime_error("Replay AABB tree has too many static-model indexes");
            for (const auto index : source.staticModelIndexes)
                if (index >= staticModels.instances.size())
                    throw std::runtime_error("Replay AABB tree references an invalid static model");
            uint8_t tree[48]{};
            source.bounds.Write(tree);
            stamp32(tree, 0x18, source.surfaceCount);
            stamp32(tree, 0x1C, source.firstSurface);
            stamp32(tree, 0x20, source.childrenOffset);
            stamp16(tree, 0x24, source.childCount);
            stamp16(tree, 0x26, static_cast<uint16_t>(source.staticModelIndexes.size()));
            if (!source.staticModelIndexes.empty())
                stamp64(tree, 0x28, PTR_FOLLOWS);
            zw.write(tree, sizeof(tree));
        }
        zw.align(1);
        for (const auto &source : cell.trees)
            for (const auto index : source.staticModelIndexes)
                zw.writeT<uint16_t>(index);
    }
    replaylightgrid::Emit(zw, lightGrid);
    zw.popStream();
    zw.popStream();
    zw.align(3);
    for (const auto &model : mesh.brushModels)
    {
        uint8_t brushModel[96]{};
        // Replay GfxBrushModel: authored local bounds +0x38, radius +0x50,
        // first surface +0x54, uint16 surface count +0x58, MDAO index +0x5C.
        model.bounds.Write(brushModel + 0x38);
        stampf(brushModel, 0x50, model.bounds.Radius());
        stamp32(brushModel, 0x54, model.firstSurface);
        stamp16(brushModel, 0x58, static_cast<uint16_t>(model.surfaceCount));
        stamp32(brushModel, 0x5C, UINT32_MAX);
        zw.write(brushModel, sizeof(brushModel));
    }
    replayrender::EmitSortedSurfaces(zw, mesh, staticModels);
    zw.align(15);
    zw.write(umbraTome.data(), umbraTome.size());
    zw.popStream();
    zw.popStream();

    // Replay allocates per-cell scene and visibility bitsets in stream 4.
    // Preload_GfxWorldPtr RVA 0xDB2EB0 and Postload RVA 0xDA6CD0 use stream 2.
    // Reserve both body destinations; only one copy is serialized.
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zw.align(15);
    zw.reserveCalc(kSizeGfxWorld);
    zw.align(31);
    zw.popStream();
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zw.align(7);
    zw.reserveCalc(0x148);
    zw.align(31);
    zw.popStream();
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    zw.align(15);
    zw.reserveCalc(0xE8);
    zw.align(31);
    zw.popStream();
    zw.pushStream(XFILE_BLOCK_SHARED_STREAM); // Replay stream 4 = zero-fill
    const size_t additionalSceneBits = cellCount > 1 ? size_t(cellCount - 1) * 512 * 4 : 0;
    const size_t additionalCellVisBits =
        cellCount > 1 ? size_t(cellCount) * cellWordCount * 4 - 4 : 0;
    const size_t additionalSunBits = cellWordCount > 1 ? size_t(cellWordCount - 1) * 4 : 0;
    const size_t additionalPrimaryLights = size_t(primaryLightCount - 2) * 0x98;
    const size_t additionalPrimaryLightVis = size_t(primaryLightVisDataCount - 1) * 4;
    const size_t smodelVisDataCount = (staticModels.instances.size() + 31) >> 5;
    const size_t additionalSmodelVis =
        smodelVisDataCount ? 24 * ((smodelVisDataCount * 4 + 127) & ~size_t(127)) : 0;
    const size_t dynEntCellBitsBytes =
        size_t(cellCount) * 4 * (dynEntWordCounts[0] + dynEntWordCounts[1]);
    const size_t dynEntViewBitsBytes =
        dynEntVisViews.size() * 32 * (dynEntWordCounts[0] + dynEntWordCounts[1]);
    const size_t dynEntSceneBytes =
        12 * (size_t(dynamicModelCount) + dynamicBrushCount);
    const size_t dynEntMotionBytes =
        4 * size_t(dynEntWordCounts[0] + dynEntWordCounts[1]);
    zw.reserveCalc(0xD00 + additionalSceneBits + additionalCellVisBits + additionalSunBits +
                   additionalPrimaryLights + additionalPrimaryLightVis + additionalSmodelVis +
                   dynEntCellBitsBytes + dynEntViewBitsBytes + dynEntSceneBytes +
                   dynEntMotionBytes);
    zw.popStream();
}

} // namespace iw8maps
