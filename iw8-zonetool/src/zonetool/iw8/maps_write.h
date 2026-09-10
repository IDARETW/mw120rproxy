#pragma once
#include "iw8_zone.h"
#include "replay_map_layout.h"
#include <cstdint>
#include <string>

namespace iw8maps
{

static constexpr size_t kSizeGlass = replaymap::GlassWorldSize;
static constexpr size_t kSizeGfxWorld = replaymap::GfxWorldSize;

// GlassWorld
static constexpr size_t kGL_name = 0x00, kGL_glassData = 0x08;
// GfxWorld offsets verified against Replay 1.20.
static constexpr size_t kGW_name = 0x00, kGW_baseName = 0x08, kGW_bspVersion = 0x10,
                        kGW_bounds = 0x78;
// dpvsPlanes(GfxWorldDpvsPlanes 0x28)@0x90: +0x00 cellCount u32, +0x04 planeCount u16, +0x08
// planes,
//   +0x10 nodeCount u16, +0x18 nodes(u16*), +0x20 sceneEntCellBits
static constexpr size_t kGW_dpvsPlanes = 0x90;
static constexpr size_t kGW_dpp_cellCount = 0x00, kGW_dpp_planeCount = 0x04, kGW_dpp_planes = 0x08,
                        kGW_dpp_nodeCount = 0x10, kGW_dpp_nodes = 0x18,
                        kGW_dpp_sceneEntCellBits = 0x20;
static constexpr size_t kGW_cells = 0xB8, kGW_cellTransientInfos = 0xC0;
// Exact Replay Load_GfxWorld RVA 0xD92E80: stream-4 visibility allocation.
static constexpr size_t kGW_cellVisBits = replaymap::CellVisBits;
// GfxCell (0x28): bounds@0x00 (24B midPoint+halfSize), portalCount u32@0x18, portals@0x20
static constexpr size_t kSizeGfxCell = 0x28, kGC_portalCount = 0x18, kGC_portals = 0x20;

// Emit a GlassWorld with initialized empty glass data.
void emitGlassMapBody(iw8::ZoneWriter &zw, const char *assetName);

// Emit the converted GfxWorld.
void emitGfxMapBody(iw8::ZoneWriter &zw, const char *assetName, const std::string &meshPath = {});

} // namespace iw8maps
