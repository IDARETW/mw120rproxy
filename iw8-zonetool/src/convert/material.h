// convert/material.h — IW3->IW8 material field/format conversion helpers (FOCUS family: material).
// Owned by the material agent. Namespaced under convert::mtl to avoid clashes with other families.
//
// The Stage-A reader (src/iw3/asset_material.cpp) dumps an IW3 Material to materials/<name>.json in the
// zonetool-IW3 dump JSON shape (see iw3/asset_material.cpp header comment). The Stage-B writer
// (src/iw8/asset_material.cpp) reads that JSON and rebuilds an IW8 Material (0x78) + MaterialTextureDef
// (0x10) array. The only non-trivial cross-format mapping is the texture SEMANTIC enum and the sort/
// material-type scalars: IW3 and IW8 number these differently. These helpers centralize that mapping so
// both the reader (for documentation) and the writer (for emission) agree.
//
// SCOPE NOTE: IW8 materials are technique-driven; the full statebits/constant-buffer pipeline depends on
// a MaterialTechniqueSet asset (techset = DEFERRED, SPEC §1). The converter therefore emits a
// technique-less Material (techniqueSet=null, constantBufferTable=null) that is STRUCTURALLY valid and
// load-safe (the loader walks only non-null pointers); the visual material resolves to the engine's
// default technique. textureTable image refs ARE rebuilt (the focus of this family).
#pragma once
#include <cstdint>

namespace convert::mtl {

// ---- texture semantic (MaterialTextureDef.semantic) ----------------------------------------------
// IW3 TextureSemantic (Structs.hpp): TS_2D=0, TS_FUNCTION=1, TS_COLOR_MAP=2, TS_NORMAL_MAP=5,
// TS_SPECULAR_MAP=8, TS_WATER_MAP=0xB. IW8 TextureSemantic (dev PDB) uses the same low ordinals for the
// common maps (2D/colour/normal/specular), so the prototype passes the IW3 value through. Water maps
// (0xB) have no IW8 equivalent in this prototype and are downgraded to a plain colour map (the bound
// image is dumped as a normal image). Returns the IW8 semantic byte.
uint8_t iw3_to_iw8_semantic(uint8_t iw3Semantic);

// True if the IW3 texture semantic is a water map (the bound "image" is actually a water_t*, NOT a
// GfxImage*). The reader must NOT dereference it as a GfxImage; it dumps the inner image name instead.
inline bool iw3_semantic_is_water(uint8_t iw3Semantic) { return iw3Semantic == 0x0B; }

// ---- material sortKey ----------------------------------------------------------------------------
// IW3 sortKey -> IW8 sortKey. The IW3 dumper (zonetool) remapped sortKey via a hand-built table for the
// IW3->IW4 path; for IW3->IW8 the safe prototype value is the engine "opaque ambient" bucket. We pass
// the IW3 value through unchanged (it is a render-ordering hint, not a load-time-walked field) and clamp
// to a byte. Centralized here so a future techset-aware pass can refine it.
uint8_t iw3_to_iw8_sortkey(uint8_t iw3SortKey);

// ---- material type / surface region --------------------------------------------------------------
// IW8 Material.materialType is MaterialGeometryType (u32). IW3 has no direct equivalent (it is implied
// by the techset). 0 = the generic/default geometry type, which is load-safe. cameraRegion: IW3 stores
// a GfxCameraRegionType byte; pass it through (clamped). Helpers exist so the field policy is one place.
uint32_t iw3_to_iw8_material_type(uint8_t iw3GameFlags);
uint8_t  iw3_to_iw8_camera_region(uint8_t iw3CameraRegion);

} // namespace convert::mtl
