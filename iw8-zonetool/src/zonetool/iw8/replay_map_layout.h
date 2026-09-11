#pragma once
#include <cstddef>

// Exact 1.20 Replay wire sizes: g_assetSizes RVA 0x2458160. The full
// iw8_map_structs.h models a later build and must not size Replay disk bodies.
// Load_MapEnts RVA 0xE0B520 reads 0x408; Load_GfxWorld RVA 0xD92E80 reads 0x4590.
namespace replaymap
{
inline constexpr size_t MapEntsSize = 0x408;
inline constexpr size_t ClipMapSize = 0xF8;
inline constexpr size_t ComWorldSize = 0xA8;
inline constexpr size_t GlassWorldSize = 0x10;
inline constexpr size_t GfxWorldSize = 0x4590;
// Load_GfxWorld allocates this in stream 4, using cellCount*ceil(cellCount/32).
inline constexpr size_t CellVisBits = 0x3EF0;
// Replay 1.20 Load_GfxWorld RVA 0xD93A41 loads an aligned byte16 array, then
// initializes the runtime Tome pointer at RVA 0xD93A87.
inline constexpr size_t UmbraTomeSize = 0x4450;
inline constexpr size_t UmbraTomeData = 0x4458;
inline constexpr size_t UmbraTome = 0x4460;
} // namespace replaymap
