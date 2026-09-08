

#pragma once
#include "iw8/iw8_focus_structs.h"
#include <cstdint>
#include <string>
#include <vector>

namespace iw8 {
class ZoneWriter;
}

namespace convdump::mtl {

// ===================================================================================================
// READER OUTPUT — the IW5 dump material, faithfully parsed (verbatim field VALUES, no conversion yet).
// ===================================================================================================
struct DumpMap {
    std::string image; // bound image asset name ("" / "$identitynormalmap" / "~...~hash" all valid)
    int semantic = 0;  // IW5 TextureSemantic (2=color,5=normal,8=specular,0xB=water,...)
    int sampleState = 0;   // IW5 sampler-state index (CAN be negative in the dump)
    int firstChar = 0;     // diagnostic/parity (IW5 firstCharacter)
    int lastChar = 0;      // diagnostic/parity (IW5 lastCharacter / secondLastCharacter)
    uint32_t typeHash = 0; // diagnostic/parity (IW5 typeHash — the map slot id)
};
struct DumpConst {
    std::string name;      // diagnostic (IW8 MaterialConstantDef has no name; nameHash drops out)
    uint32_t nameHash = 0; // diagnostic
    float literal[4] = {0, 0, 0, 0};
};
struct DumpStateBits {
    uint32_t loadBits[2] = {0, 0};
}; // one GfxStateBits.loadBits pair per stateMap entry

struct DumpMaterial {
    bool loaded = false;
    std::string name; // includes the 2-char prefix dir ("mc/lambert1")
    std::string
        techniqueSetName; // "techniqueSet->name" (informational; techset DEFERRED -> null ptr)
    int gameFlags = 0;
    int sortKey = 0;
    int stateFlags = 0;
    int cameraRegion = 0;
    uint32_t surfaceTypeBits = 0;
    int animationX = 0, animationY = 0;
    std::vector<DumpMap> maps;
    std::vector<DumpConst> constants;    // may be empty (null/absent constantTable)
    std::vector<DumpStateBits> stateMap; // informational (techset-owned in IW8); not emitted
};

// Parse a single material dump file (the absolute path to materials/<2char>/<name>) into a DumpMaterial.
// Returns d.loaded=false (and logs) on missing file / malformed JSON. `displayName` (optional) overrides
// the logged/struct name when the JSON has no "name".
DumpMaterial readMaterialJson(const std::string& filePath, const std::string& displayName = "");

// ===================================================================================================
// CONVERTER OUTPUT — an IW8-ready material: the 0x78 struct with SCALAR fields filled + the side tables
// the writer serializes (texture bindings, constants, submaterials). Pointer fields in `mat` are left
// ZEROED here; the writer stamps the tagged sentinels at emit time.
// ===================================================================================================
struct Iw8TexBinding {
    uint8_t index;         // IW8 MaterialTextureDef.index (sampler/dest slot)
    std::string imageName; // bound GfxImage name ("" => null ptr, load-safe)
    uint8_t semantic;      // converted IW8 semantic (diagnostic / future link use)
    uint8_t samplerState;  // passthrough (clamped)
};
struct Iw8Material {
    bool ok = false;
    std::string name;
    iw8_focus::Material mat;             // 0x78; scalars filled, ptr slots zeroed
    std::vector<Iw8TexBinding> textures; // -> textureTable[textureCount]
    std::vector<iw8_focus::MaterialConstantDef>
        constants;                         // -> constantTable[constantCount] (stride 20)
    std::vector<std::string> subMaterials; // -> subMaterials[layerCount] (none for IW5 dump)
};

// Map the parsed IW5 dump material onto the IW8 0x78 model (scalar field conversion + slot assignment).
Iw8Material convert(const DumpMaterial& d);

// Serialize one converted IW8 material into the ZoneWriter as a top-level material(11) asset (registers
// via zw.add; the body runs in VIRTUAL(8) and writes the 0x78 struct + followed payloads in field order).
// Returns false (and logs) if im.ok is false.
bool writeMaterial(iw8::ZoneWriter& zw, const Iw8Material& im);

bool emitMaterialFromDump(iw8::ZoneWriter& zw,
                          const std::string& filePath,
                          const std::string& displayName = "");

uint8_t iw5_to_iw8_semantic(int iw5Semantic);
uint8_t iw5_to_iw8_sortkey(int iw5SortKey);
uint32_t iw5_to_iw8_material_type(int iw5GameFlags);
uint8_t iw5_to_iw8_camera_region(int iw5CameraRegion);
// True if the IW5 semantic is a water map (the bound "image" is a water_t* in-game; the dump already
// flattened it to the inner image NAME, so the reader just records the name — documented for the writer).
inline bool iw5_semantic_is_water(int iw5Semantic) {
    return iw5Semantic == 0x0B;
}
// Sampler/dest slot for a texture at array position `arrayIndex` (engine default technique tolerates
// sequential binding). Kept as a function so a techset-aware pass can refine it.
uint8_t slot_for_index(size_t arrayIndex);

} // namespace convdump::mtl
