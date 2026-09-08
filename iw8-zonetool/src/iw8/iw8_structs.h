

#pragma once
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

enum IW8_XAssetType : int32_t {
    ASSET_TYPE_PHYSICSASSET = 3,
    ASSET_TYPE_XANIM = 7,
    ASSET_TYPE_XMODELSURFS = 8, // <- focus
    ASSET_TYPE_XMODEL = 9,      // <- focus
    ASSET_TYPE_MATERIAL = 11,   // <- focus
    ASSET_TYPE_COMPUTESHADER = 12,
    ASSET_TYPE_LIBSHADER = 13,
    ASSET_TYPE_VERTEXSHADER = 14,
    ASSET_TYPE_HULLSHADER = 15,
    ASSET_TYPE_DOMAINSHADER = 16,
    ASSET_TYPE_PIXELSHADER = 17,
    ASSET_TYPE_TECHSET = 18,
    ASSET_TYPE_IMAGE = 19, // <- focus
    ASSET_TYPE_SOUNDBANK = 21,
    ASSET_TYPE_COL_MAP = 23,   // clipMap_t          <- focus (maps)
    ASSET_TYPE_COM_MAP = 24,   // ComWorld
    ASSET_TYPE_GLASS_MAP = 25, // GlassWorld
    ASSET_TYPE_AIPATHS = 26,
    ASSET_TYPE_NAVMESH = 27,
    ASSET_TYPE_TACGRAPH = 28,
    ASSET_TYPE_MAP_ENTS = 29, // MapEnts (+ dynentitylist) <- focus (maps)
    ASSET_TYPE_FX_MAP = 30,   // FxWorld
    ASSET_TYPE_GFX_MAP = 31,  // GfxWorld
    ASSET_TYPE_GFX_MAP_TRZONE = 32,
    ASSET_TYPE_IESPROFILE = 33,
    ASSET_TYPE_LIGHTDEF = 34,
    ASSET_TYPE_GRADINGCLUT = 35,
    ASSET_TYPE_UI_MAP = 36,
    ASSET_TYPE_FOGSPLINE = 37,
    ASSET_TYPE_ANIMCLASS = 38,
    ASSET_TYPE_PLAYERANIM = 39,
    ASSET_TYPE_RAWFILE = 51,
    ASSET_TYPE_SCRIPTFILE = 52,
    ASSET_TYPE_STRINGTABLE = 54,
    ASSET_TYPE_LUAFILE = 62,
    ASSET_TYPE_COUNT = 0x100,
};

// ---- g_assetSizes (retail @0x4A9D7F0). The writer struct for each type MUST sizeof-equal this. ---
namespace iw8sz {
constexpr size_t XANIM = 0xA0;
constexpr size_t XMODELSURFS = 0x60;
constexpr size_t XMODEL = 0x2B0;
constexpr size_t MATERIAL = 0x78;
constexpr size_t TECHSET = 0x40;
constexpr size_t IMAGE = 0xE8;
constexpr size_t SOUNDBANK = 0x200;
constexpr size_t COL_MAP = 0xF8;
constexpr size_t COM_MAP = 0xA8;
constexpr size_t GLASS_MAP = 0x10;
constexpr size_t MAP_ENTS = 0x428;
constexpr size_t FX_MAP = 0x3CD0;
constexpr size_t GFX_MAP = 0x45D0;
constexpr size_t GFX_MAP_TRZONE = 0x148;
constexpr size_t LIGHTDEF = 0x20;

inline const char* type_name(int t) {
    switch (t) {
    case ASSET_TYPE_XMODELSURFS:
        return "xmodelsurfs";
    case ASSET_TYPE_XMODEL:
        return "xmodel";
    case ASSET_TYPE_MATERIAL:
        return "material";
    case ASSET_TYPE_IMAGE:
        return "image";
    case ASSET_TYPE_TECHSET:
        return "techset";
    case ASSET_TYPE_COL_MAP:
        return "col_map";
    case ASSET_TYPE_COM_MAP:
        return "com_map";
    case ASSET_TYPE_GLASS_MAP:
        return "glass_map";
    case ASSET_TYPE_MAP_ENTS:
        return "map_ents";
    case ASSET_TYPE_FX_MAP:
        return "fx_map";
    case ASSET_TYPE_GFX_MAP:
        return "gfx_map";
    case ASSET_TYPE_LIGHTDEF:
        return "lightdef";
    case ASSET_TYPE_RAWFILE:
        return "rawfile";
    default:
        return "unknown";
    }
}
}

// ---- Zone-stream root objects (16/16/32/24) — load-read-order serialized -----------------------
struct IW8_XAsset {
    int32_t type;    // 0x00
    uint32_t _pad04; // 0x04
    uint64_t header; // 0x08  tagged pointer in-zone
};
static_assert(sizeof(IW8_XAsset) == 16, "XAsset 16");

struct IW8_ScriptStringList {
    int32_t count;  // 0x00
    uint8_t loaded; // 0x04
    uint8_t _pad05[3];
    uint64_t strings; // 0x08  const char** (tagged ptr)
};
static_assert(sizeof(IW8_ScriptStringList) == 16, "ScriptStringList 16");

struct IW8_XAssetList {
    IW8_ScriptStringList stringList; // 0x00 (16)
    uint32_t assetCount;             // 0x10
    uint32_t assetReadPos;           // 0x14
    uint64_t assets;                 // 0x18  XAsset* (tagged ptr)
};
static_assert(sizeof(IW8_XAssetList) == 32, "XAssetList 32");

struct IW8_RawFile {
    uint64_t name;          // 0x00  const char* (tagged ptr)
    uint32_t compressedLen; // 0x08  0 = stored uncompressed
    uint32_t len;           // 0x0C
    uint64_t buffer;        // 0x10  const char* (tagged ptr)
};
static_assert(sizeof(IW8_RawFile) == 24, "RawFile 24");

#pragma pack(pop)
