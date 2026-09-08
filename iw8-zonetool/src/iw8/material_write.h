// iw8/material_write.h — material-family Stage-B writer helpers (owned by the material agent).
// Namespaced under iw8mtl to avoid clashes. Builds the IW8 Material (0x78) + MaterialTextureDef (0x10)
// array load image into the ZoneWriter, following the load-read-order pointer-stamping convention
// (ZONE_LOAD_MODEL.md): every non-null pointer field is stamped -2 (follows) and its pointed-to data is
// emitted inline in STRUCT-FIELD ORDER right after the struct, because the IW8 loader walks the struct's
// pointer fields top-to-bottom reading each followed payload from the current stream.
#pragma once
#include "iw8/iw8_focus_structs.h"
#include <cstdint>
#include <string>
#include <vector>

namespace iw8mtl {

// One parsed texture binding from the zone-source JSON "textureTable"/"maps" entry.
struct TexBinding {
    uint8_t     index;       // sampler/dest slot (IW8 MaterialTextureDef.index). Derived from semantic.
    std::string imageName;   // bound GfxImage name ("" => no image -> null ptr, load-safe)
    uint8_t     semantic;    // IW8 semantic (converted from IW3)
    uint8_t     samplerState;// passthrough
    int8_t      firstChar;   // diagnostic/parity (not emitted in IW8 struct)
    int8_t      lastChar;    // diagnostic/parity
    uint32_t    typeHash;    // diagnostic/parity
};

// Fully-parsed material ready to serialize. Pointer targets are names; the writer stamps sentinels.
struct ParsedMaterial {
    std::string             name;
    iw8_focus::Material     mat;       // the 0x78 struct (scalar fields filled; ptr fields zeroed here)
    std::vector<TexBinding> textures;  // textureTable[textureCount]
    std::vector<iw8_focus::MaterialConstantDef> constants; // constantTable[constantCount]
    std::vector<std::string> subMaterials;                  // subMaterials[layerCount]
};

} // namespace iw8mtl
