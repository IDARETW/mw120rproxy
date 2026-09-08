// Replay 1.20 map-family wire emitter. Rendering and collision remain incomplete.
#pragma once
#include "iw8_zone.h"
#include "replay_map_layout.h"
#include <cstdint>
#include <string>

namespace iw8maps {

// Replay disk bodies, independent of the legacy full C++ structure definitions.
static constexpr size_t kSizeMapEnts = replaymap::MapEntsSize;
static constexpr size_t kSizeClipMap = replaymap::ClipMapSize;
static constexpr size_t kSizeComWorld = replaymap::ComWorldSize;
static constexpr size_t kSizeGlass = replaymap::GlassWorldSize;
static constexpr size_t kSizeGfxWorld = replaymap::GfxWorldSize;

// ---- field offsets (retail-confirmed, fastfile_research/10) --------------------------------------
// MapEnts
static constexpr size_t kME_name = 0x00, kME_entityString = 0x08, kME_numEntityChars = 0x10;
// clipMap_t
static constexpr size_t kCM_name = 0x00, kCM_isInUse = 0x08, kCM_mapEnts = 0x18,
                        kCM_bpMin = 0x80, kCM_bpMax = 0x8C,
                        kCM_havokSize = 0xB8, kCM_havokData = 0xC0, kCM_checksum = 0xF0;
// ComWorld
static constexpr size_t kCW_name = 0x00, kCW_isInUse = 0x08;
// GlassWorld
static constexpr size_t kGL_name = 0x00, kGL_glassData = 0x08;
// GfxWorld (HEAD offsets dev==retail AUTHORITATIVE, <0x3688; workflow wvm3x26x4)
static constexpr size_t kGW_name = 0x00, kGW_baseName = 0x08, kGW_bspVersion = 0x10, kGW_bounds = 0x78;
// dpvsPlanes(GfxWorldDpvsPlanes 0x28)@0x90: +0x00 cellCount u32, +0x04 planeCount u16, +0x08 planes,
//   +0x10 nodeCount u16, +0x18 nodes(u16*), +0x20 sceneEntCellBits
static constexpr size_t kGW_dpvsPlanes = 0x90;
static constexpr size_t kGW_dpp_cellCount = 0x00, kGW_dpp_planeCount = 0x04, kGW_dpp_planes = 0x08,
                        kGW_dpp_nodeCount = 0x10, kGW_dpp_nodes = 0x18, kGW_dpp_sceneEntCellBits = 0x20;
static constexpr size_t kGW_cells = 0xB8, kGW_cellTransientInfos = 0xC0;
// Exact Replay Load_GfxWorld RVA 0xD92E80: stream-4 visibility allocation.
static constexpr size_t kGW_cellVisBits = replaymap::CellVisBits;
// GfxCell (0x28): bounds@0x00 (24B midPoint+halfSize), portalCount u32@0x18, portals@0x20
static constexpr size_t kSizeGfxCell = 0x28, kGC_portalCount = 0x18, kGC_portals = 0x20;

// Emit a standalone MapEnts(29) body: struct(0x408)->TEMP_PRELOAD, name+entityString->VIRTUAL.
// `ents` is the IW8 numeric-keyId entityString (NUL appended here; numEntityChars = size+1).
void emitMapEntsBody(iw8::ZoneWriter& zw, const char* assetName, const std::string& ents);

// Emit a col_map(23) clipMap_t body: struct(0xF8)->TEMP_PRELOAD with null havok + generous broadphase
// AABB, name->VIRTUAL, then the cross-linked MapEnts inline (mapEnts=-2 follows) so the clipMap's
// mapEnts pointer resolves (the byte-safe duplicate, exactly as map_zone.h does).
void emitColMapBody(iw8::ZoneWriter& zw, const char* assetName, const std::string& ents);

// Emit a com_map(24) ComWorld body — structural prototype (name + zeros).
void emitComMapBody(iw8::ZoneWriter& zw, const char* assetName);

// Emit a glass_map(25) GlassWorld body — structural prototype (name + null g_glassData).
void emitGlassMapBody(iw8::ZoneWriter& zw, const char* assetName);

// Emit a gfx_map(31) GfxWorld body — structural prototype (name + baseName + zeros).
void emitGfxMapBody(iw8::ZoneWriter& zw, const char* assetName, const std::string& meshPath = {});

} // namespace iw8maps
