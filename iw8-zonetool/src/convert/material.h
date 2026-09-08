

#pragma once
#include <cstdint>

namespace convert::mtl {

uint8_t iw3_to_iw8_semantic(uint8_t iw3Semantic);

// True if the IW3 texture semantic is a water map (the bound "image" is actually a water_t*, NOT a
// GfxImage*). The reader must NOT dereference it as a GfxImage; it dumps the inner image name instead.
inline bool iw3_semantic_is_water(uint8_t iw3Semantic) {
    return iw3Semantic == 0x0B;
}

uint8_t iw3_to_iw8_sortkey(uint8_t iw3SortKey);

// ---- material type / surface region --------------------------------------------------------------
// IW8 Material.materialType is MaterialGeometryType (u32). IW3 has no direct equivalent (it is implied
// by the techset). 0 = the generic/default geometry type, which is load-safe. cameraRegion: IW3 stores
// a GfxCameraRegionType byte; pass it through (clamped). Helpers exist so the field policy is one place.
uint32_t iw3_to_iw8_material_type(uint8_t iw3GameFlags);
uint8_t iw3_to_iw8_camera_region(uint8_t iw3CameraRegion);

} // namespace convert::mtl
