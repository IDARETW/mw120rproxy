

#pragma once
#include "iw8/iw8_focus_structs.h"
#include <cstdint>
#include <string>
#include <vector>

namespace iw8mtl {

// One parsed texture binding from the zone-source JSON "textureTable"/"maps" entry.
struct TexBinding {
    uint8_t index; // sampler/dest slot (IW8 MaterialTextureDef.index). Derived from semantic.
    std::string imageName; // bound GfxImage name ("" => no image -> null ptr, load-safe)
    uint8_t semantic;      // IW8 semantic (converted from IW3)
    uint8_t samplerState;  // passthrough
    int8_t firstChar;      // diagnostic/parity (not emitted in IW8 struct)
    int8_t lastChar;       // diagnostic/parity
    uint32_t typeHash;     // diagnostic/parity
};

// Fully-parsed material ready to serialize. Pointer targets are names; the writer stamps sentinels.
struct ParsedMaterial {
    std::string name;
    iw8_focus::Material mat; // the 0x78 struct (scalar fields filled; ptr fields zeroed here)
    std::vector<TexBinding> textures;                      // textureTable[textureCount]
    std::vector<iw8_focus::MaterialConstantDef> constants; // constantTable[constantCount]
    std::vector<std::string> subMaterials;                 // subMaterials[layerCount]
};

} // namespace iw8mtl
