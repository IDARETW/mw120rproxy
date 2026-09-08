// conv_material.cpp — DUMP-PATH material IW5->IW8 field/format conversion (Tier2, FLAG-GATED).
// Maps a parsed IW5 dump material (convdump::mtl::DumpMaterial) onto the IW8 material(11)=0x78 model
// (convdump::mtl::Iw8Material). Only the field VALUES carry across; the IW8 0x78 layout is built fresh.
//
// SCOPE (matches the legacy path's load-safe decision): IW8 materials are technique-driven. The full
// statebits / constant-buffer pipeline depends on a MaterialTechniqueSet asset (techset = DEFERRED,
// SPEC §1). The converter therefore produces a technique-LESS material: techniqueSet=null,
// constantBufferTable=null, drawSurf zeroed. That is STRUCTURALLY valid + load-safe (the loader walks
// only non-null pointers); the visual material resolves to the engine's default technique. The texture
// image NAMES + slot index + semantic ARE preserved (the focus of this family) so a later cross-zone
// link pass can patch MaterialTextureDef.image. Self-contained under convdump::mtl.
#include "dumpsrc/material_dumpsrc.h"
#include <cstring>

using namespace iw8_focus;

namespace convdump::mtl {

// ---- semantic ------------------------------------------------------------------------------------
// IW5 TextureSemantic == IW8 TextureSemantic for the common low ordinals (2D=0, color=2, normal=5,
// specular=8). Water(0xB) has no IW8 material path here -> downgrade to color(2). Clamp to a byte.
uint8_t iw5_to_iw8_semantic(int iw5Semantic) {
    if (iw5Semantic == 0x0B) return 2;          // water -> color (image treated as a plain image)
    if (iw5Semantic < 0 || iw5Semantic > 255) return 0;
    return static_cast<uint8_t>(iw5Semantic);
}

// ---- sortKey -------------------------------------------------------------------------------------
// Render-ordering hint (NOT a load-time-walked field). Passed through unchanged, clamped to a byte. A
// future techset-aware pass can remap via a hand-built bucket table (as the IW3->IW4 path did).
uint8_t iw5_to_iw8_sortkey(int iw5SortKey) {
    return static_cast<uint8_t>(iw5SortKey & 0xFF);
}

// ---- materialType --------------------------------------------------------------------------------
// IW8 Material.materialType is MaterialGeometryType (u32). IW5 has no direct equivalent (it is implied by
// the techset). 0 = the generic/default geometry type, which is load-safe. (iw5GameFlags is unused for
// now; kept as the input so a future heuristic can derive a type from it.)
uint32_t iw5_to_iw8_material_type(int /*iw5GameFlags*/) {
    return 0;
}

// ---- cameraRegion --------------------------------------------------------------------------------
// IW8 Material.cameraRegion is a GfxCameraRegionType byte. Pass the IW5 value through, clamped.
uint8_t iw5_to_iw8_camera_region(int iw5CameraRegion) {
    return static_cast<uint8_t>(iw5CameraRegion & 0xFF);
}

// ---- texture slot --------------------------------------------------------------------------------
uint8_t slot_for_index(size_t arrayIndex) {
    return static_cast<uint8_t>(arrayIndex & 0xFF);
}

// ===================================================================================================
Iw8Material convert(const DumpMaterial& d) {
    Iw8Material out;
    if (!d.loaded) return out; // ok=false

    out.name = d.name;

    Material& m = out.mat;
    std::memset(&m, 0, sizeof(m)); // all pointer + scalar fields zeroed; writer stamps the ptr sentinels

    // --- scalar field conversion (IW5 -> IW8) ---
    m.contents        = 0;                                       // IW5 dump has no content bits at material level
    m.surfaceFlags    = d.surfaceTypeBits;                       // IW5 surfaceTypeBits -> IW8 surfaceFlags
    m.maxDisplacement = 0.0f;
    m.materialType    = iw5_to_iw8_material_type(d.gameFlags);
    m.cameraRegion    = iw5_to_iw8_camera_region(d.cameraRegion);
    m.sortKey         = iw5_to_iw8_sortkey(d.sortKey);
    // _u7 (anon u16 flags @0x1A): the IW5 "stateFlags" is an 8-bit render flag set; carry it into the low
    // byte of _u7 (load-safe — not a count/pointer; purely a render hint the default technique ignores).
    m._u7             = static_cast<uint16_t>(d.stateFlags & 0xFF);
    m.packedAtlasDataSize     = 0;
    m.textureAtlasRowCount    = 0;
    m.textureAtlasColumnCount = 0;
    // drawSurf[16] left zeroed (runtime-built render key; zero is load-safe).

    // --- texture bindings (maps[]) ---
    size_t idx = 0;
    for (const auto& mp : d.maps) {
        Iw8TexBinding b;
        b.index        = slot_for_index(idx);
        b.imageName    = mp.image;                                // "" => null ptr (load-safe)
        b.semantic     = iw5_to_iw8_semantic(mp.semantic);
        // sampleState may be negative in the dump; reinterpret the low byte (the engine sampler index is a
        // small unsigned value; the sign is an artifact of the IW5 signed-char field).
        b.samplerState = static_cast<uint8_t>(mp.sampleState & 0xFF);
        out.textures.push_back(std::move(b));
        ++idx;
    }

    // --- constantTable: IW8 MaterialConstantDef = { index(u8), literal(vec4) } (stride 0x14). The IW5
    //     dump carries {name, nameHash, literal}; IW8 has no name -> drop it, map slot to array position. ---
    for (size_t i = 0; i < d.constants.size(); ++i) {
        MaterialConstantDef c;
        std::memset(&c, 0, sizeof(c));
        c.index = static_cast<uint8_t>(i & 0xFF);
        for (int k = 0; k < 4; ++k) c.literal.v[k] = d.constants[i].literal[k];
        out.constants.push_back(c);
    }

    // --- subMaterials: the IW5 dump has none (single-layer material) -> layerCount 0. ---

    // counts -> the //CNT fields governing the followed tables
    m.textureCount        = static_cast<uint8_t>(out.textures.size() & 0xFF);
    m.constantCount       = static_cast<uint8_t>(out.constants.size() & 0xFF);
    m.constantBufferCount = 0;
    m.layerCount          = static_cast<uint8_t>(out.subMaterials.size() & 0xFF);

    out.ok = true;
    return out;
}

} // namespace convdump::mtl
