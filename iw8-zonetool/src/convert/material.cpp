// convert/material.cpp — IW3->IW8 material conversion helpers (impl of convert/material.h).
#include "material.h"

namespace convert::mtl {

uint8_t iw3_to_iw8_semantic(uint8_t iw3Semantic) {
    // IW3 and IW8 share the low TextureSemantic ordinals for the maps we care about:
    //   0 TS_2D, 1 TS_FUNCTION, 2 TS_COLOR_MAP, 5 TS_NORMAL_MAP, 8 TS_SPECULAR_MAP.
    // Water maps (0xB) have no IW8 GfxImage-bound equivalent here -> downgrade to a colour map so the
    // bound (inner) image still binds to a sampler slot.
    if (iw3Semantic == 0x0B) return 0x02; // water -> colour map
    return iw3Semantic;
}

uint8_t iw3_to_iw8_sortkey(uint8_t iw3SortKey) {
    // Pass-through (render-ordering hint, not load-walked). Clamp is implicit (already a byte).
    return iw3SortKey;
}

uint32_t iw3_to_iw8_material_type(uint8_t /*iw3GameFlags*/) {
    // 0 = the generic/default MaterialGeometryType (load-safe; the engine default technique applies).
    return 0u;
}

uint8_t iw3_to_iw8_camera_region(uint8_t iw3CameraRegion) {
    // Pass-through; both engines use small GfxCameraRegionType bytes. 0xFF/garbage clamps to NONE(4).
    if (iw3CameraRegion > 11) return 4; // CAMERA_REGION_NONE
    return iw3CameraRegion;
}

} // namespace convert::mtl
