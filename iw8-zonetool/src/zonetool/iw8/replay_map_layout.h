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
// Replay Load_GfxWorldDraw RVA 0xD96710: the map-specific compressed sun
// shadow payload precedes its 0x60-byte GPU buffer and 0x30-byte parameters.
// These are whole-GfxWorld offsets, not the later-build GfxWorldDraw offsets.
// Replay cs_sunvis.434.cso reads the payload as a 32-byte header, a four-byte
// forest, and pointed-to 512-pixel mini-trees. A nonzero scalar forest entry
// is a normalized depth threshold under the outer header's +24/+28 origin
// and inverse span; mini-tree leaves use the mini-header's own +24/+28 pair.
// Consequently these fields must be baked together from this map's casters.
inline constexpr size_t CompressedSunShadowSize = 0x37F4;
inline constexpr size_t CompressedSunShadowData = 0x37F8;
inline constexpr size_t CompressedSunShadowBuffer = 0x3800;
inline constexpr size_t CompressedSunShadowParams = 0x3860;
// Load_GfxWorld allocates this in stream 4, using cellCount*ceil(cellCount/32).
inline constexpr size_t CellVisBits = 0x3EF0;
inline constexpr size_t SceneDynModel = 0x3F00;
inline constexpr size_t SceneDynBrush = 0x3F08;
inline constexpr size_t DynEntMotionBitsEntries = 0x3F48;
inline constexpr size_t DynEntMotionBits = 0x3F50;
// GfxWorldDpvsStatic begins at +0x3F98. Replay clears these four resident
// visibility families from the owning world counts during client startup.
// A nonzero owner count paired with a null pointer reaches the unguarded
// fill calls at 0x18EC679/0x18EC6A1/0x18EC6C9/0x18EC6F1.
inline constexpr size_t DpvsStatic = 0x3F98;
inline constexpr size_t PrimaryLightVisDataCount = DpvsStatic + 0x08;
inline constexpr size_t ReflectionProbeVisDataCount = DpvsStatic + 0x0C;
inline constexpr size_t VolumetricVisDataCount = DpvsStatic + 0x10;
inline constexpr size_t DecalVisDataCount = DpvsStatic + 0x14;
inline constexpr size_t PrimaryLightVisData = 0x41C0;
inline constexpr size_t ReflectionProbeVisData = 0x41C8;
inline constexpr size_t VolumetricVisData = 0x41D0;
inline constexpr size_t DecalVisData = 0x41D8;
// Load_GfxWorld's DPVS-dynamic child starts at +0x4210. Loader D93BB0
// allocates dynEntCellBits[basis] as wordCount[basis] * cellCount dwords,
// then each present dynEntVisData slot as 32 * wordCount[basis] bytes.
inline constexpr size_t DynEntDpvs = 0x4210;
inline constexpr size_t DynEntDpvsCellBits = DynEntDpvs + 0x10;
inline constexpr size_t DynEntDpvsVisModel = DynEntDpvs + 0x20;
inline constexpr size_t DynEntDpvsVisBrush = DynEntDpvs + 0x128;
// Replay 1.20 Load_GfxWorld RVA 0xD93A41 loads an aligned byte16 array, then
// initializes the runtime Tome pointer at RVA 0xD93A87.
inline constexpr size_t UmbraTomeSize = 0x4450;
inline constexpr size_t UmbraTomeData = 0x4458;
inline constexpr size_t UmbraTome = 0x4460;
} // namespace replaymap
