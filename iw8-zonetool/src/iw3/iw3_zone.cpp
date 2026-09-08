

#include "iw3_zone.h"
#include "../common/ff_io.h"
#include "../common/log.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace iw3 {

// ---- LoadCtx -------------------------------------------------------------------------------------
bool LoadCtx::read(void* dst, size_t n) {
    if (pos_ + n > zone_.size()) {
        zt::err("iw3 LoadCtx: read past end (%zu+%zu > %zu)", pos_, n, zone_.size());
        return false;
    }
    std::memcpy(dst, zone_.data() + pos_, n);
    pos_ += n;
    return true;
}

size_t LoadCtx::resolve(uint32_t v, bool* follows) const {
    if (follows)
        *follows = false;
    if (v == 0u)
        return npos;        // null
    if (v == 0xFFFFFFFFu) { // follows inline
        if (follows)
            *follows = true;
        return pos_;
    }
    // packed Offset{value:28, stream:4}, stored as (packed+1) by the writer (ZoneBuffer.hpp:130).
    uint32_t packed = v - 1u;
    uint32_t value = packed & 0x0FFFFFFFu;
    uint32_t stream = (packed >> 28) & 0xF;
    size_t base = (stream < streamBase_.size()) ? streamBase_[stream] : 0;
    return base + value;
}

// ---- dispatch table ------------------------------------------------------------------------------
namespace {
std::unordered_map<int, LoadFn>& table() {
    static std::unordered_map<int, LoadFn> t;
    return t;
}
}

void registerLoad(int iw3Type, LoadFn fn) {
    table()[iw3Type] = fn;
}
LoadFn getLoad(int iw3Type) {
    auto it = table().find(iw3Type);
    return it == table().end() ? nullptr : it->second;
}

// ---- IW3 asset-type names + IW8 mapping (CoD4 enum, Structs.hpp:15) ------------------------------
static const char* kIw3Names[] = {
    "xmodelpieces", "physpreset", "xanim",       "xmodel",      "material",
    "techset",      "image",      "sound",       "sndcurve",    "loaded_sound",
    "col_map_sp",   "col_map_mp", "com_map",     "game_map_sp", "game_map_mp",
    "map_ents",     "gfx_map",    "lightdef",    "ui_map",      "font",
    "menufile",     "menu",       "localize",    "weapon",      "snddriverglobals",
    "fx",           "impactfx",   "aitype",      "mptype",      "character",
    "xmodelalias",  "rawfile",    "stringtable",
};
const char* iw3TypeName(int t) {
    if (t >= 0 && t < (int)(sizeof(kIw3Names) / sizeof(kIw3Names[0])))
        return kIw3Names[t];
    return "type?";
}
const char* iw3ToIw8TypeName(int t) {
    switch (t) {
    case 3:
        return "xmodel";
    case 4:
        return "material";
    case 5:
        return "techset";
    case 6:
        return "image";
    case 10:
    case 11:
        return "col_map"; // col_map_sp/mp -> clipMap_t
    case 12:
        return "com_map";
    case 15:
        return "map_ents";
    case 16:
        return "gfx_map";
    case 17:
        return "lightdef";
    case 31:
        return "rawfile";
    case 32:
        return "stringtable";
    default:
        return nullptr; // no direct IW8 mapping (sound/fx/xanim/etc.)
    }
}

static bool stub_material(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: material");
    return true;
}
static bool stub_image(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: image");
    return true;
}
static bool stub_xmodel(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: xmodel");
    return true;
}
static bool stub_xsurface(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: xmodelsurfs");
    return true;
}
static bool stub_clipmap(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: col_map");
    return true;
}
static bool stub_comworld(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: com_map");
    return true;
}
static bool stub_mapents(LoadCtx&, iw3sr::ZoneSource&) {
    zt::info("iw3 Load: unsupported asset type: map_ents");
    return true;
}

void installStubLoaders() {
    registerLoad(IW3_ASSET_MATERIAL, stub_material);
    registerLoad(IW3_ASSET_IMAGE, stub_image);
    registerLoad(IW3_ASSET_XMODEL, stub_xmodel);
    registerLoad(IW3_ASSET_XMODELSURFS, stub_xsurface);
    registerLoad(IW3_ASSET_CLIPMAP, stub_clipmap);
    registerLoad(IW3_ASSET_COMWORLD, stub_comworld);
    registerLoad(IW3_ASSET_MAPENTS, stub_mapents);
}

// ---- top-level driver ----------------------------------------------------------------------------
bool loadZone(const zt::Iw3Fastfile& ff, iw3sr::ZoneSource& zs, const std::string& mapName) {
    const std::vector<uint8_t>& zone = ff.zone;
    if (zone.size() < sizeof(Iw3XFileHeader) + sizeof(Iw3XAssetList)) {
        zt::err("iw3 loadZone: zone too small (%zu)", zone.size());
        return false;
    }
    if (!getLoad(IW3_ASSET_MAPENTS))
        installStubLoaders();

    // 1) XFile header (44B): {size, externalSize, blockSize[9]}; streams 0..8 concat after it.
    Iw3XFileHeader hdr{};
    std::memcpy(&hdr, zone.data(), sizeof(hdr));
    std::vector<size_t> bases(kIw3NumStreams, 0);
    size_t cur = sizeof(Iw3XFileHeader);
    for (int i = 0; i < kIw3NumStreams; ++i) {
        bases[i] = cur;
        cur += hdr.blockSize[i];
    }
    LoadCtx lc(zone);
    lc.setStreamBases(bases);
    lc.seek(sizeof(Iw3XFileHeader));

    // 2) XAssetList root at offset 44 (start of stream 0).
    Iw3XAssetList al{};
    if (!lc.read(al))
        return false;
    zt::info("iw3 loadZone: size=0x%X extSize=0x%X scriptStrings=%u assetCount=%u", hdr.size,
             hdr.externalSize, al.scriptStringCount, al.assetCount);
    if (al.assetCount == 0 || al.assetCount > 1000000u) {
        zt::err("iw3 loadZone: implausible assetCount=%u (header parse off)", al.assetCount);
        return false;
    }

    // 3) ScriptStringList: scriptStringCount u32 pointers, then each (-1) string inline (byte-exact;
    //    validated against the real cod4builds zones — no alignment padding between these in stream 0).
    std::vector<std::string> scriptStrings;
    {
        std::vector<uint32_t> ptrs(al.scriptStringCount);
        for (uint32_t i = 0; i < al.scriptStringCount; ++i)
            if (!lc.read(ptrs[i]))
                return false;
        for (uint32_t i = 0; i < al.scriptStringCount; ++i) {
            if (ptrs[i] == 0xFFFFFFFFu) {
                size_t s = lc.pos();
                while (lc.pos() < zone.size() && zone[lc.pos()] != 0)
                    lc.seek(lc.pos() + 1);
                scriptStrings.emplace_back(reinterpret_cast<const char*>(zone.data()) + s,
                                           lc.pos() - s);
                lc.seek(lc.pos() + 1); // skip NUL
            }
        }
    }

    // 4) Contiguous XAsset[assetCount] array ({int type; u32 ptr}). Enumerate every asset's type.
    std::map<int, int> hist;
    std::vector<int> order;
    order.reserve(al.assetCount);
    for (uint32_t i = 0; i < al.assetCount; ++i) {
        Iw3XAssetEntry e{};
        if (!lc.read(e)) {
            zt::err("iw3 loadZone: asset array truncated at %u/%u", i, al.assetCount);
            return false;
        }
        hist[e.type]++;
        order.push_back(e.type);
    }

    // 5) Stage-A inventory: write the zone report + log the histogram (PROOF the offline walk is exact).
    char line[256];
    std::string rep = "IW3 zone inventory: " + mapName + "\n";
    std::snprintf(line, sizeof line, "header.size=0x%X externalSize=0x%X streams=%d\n", hdr.size,
                  hdr.externalSize, kIw3NumStreams);
    rep += line;
    std::snprintf(line, sizeof line, "scriptStrings=%u assetCount=%u\n\n", al.scriptStringCount,
                  al.assetCount);
    rep += line;
    rep += "-- asset histogram (iw3type[ordinal] x count : iw8type) --\n";
    zt::info("iw3 loadZone: %u assets across %zu types:", al.assetCount, hist.size());
    for (auto& kv : hist) {
        const char* i8 = iw3ToIw8TypeName(kv.first);
        std::snprintf(line, sizeof line, "  %-14s[%2d] x %-4d : %s\n", iw3TypeName(kv.first),
                      kv.first, kv.second, i8 ? i8 : "(no iw8 map)");
        rep += line;
        zt::info("    %-14s[%2d] x %d%s%s", iw3TypeName(kv.first), kv.first, kv.second,
                 i8 ? " -> " : "", i8 ? i8 : "");
    }
    rep += "\n-- first 24 script strings --\n";
    for (size_t i = 0; i < scriptStrings.size() && i < 24; ++i)
        rep += "  " + scriptStrings[i] + "\n";
    zs.writeText(mapName + ".zonereport.txt", rep);
    zt::info("iw3 loadZone: wrote %s.zonereport.txt (full asset inventory)", mapName.c_str());

    return true;
}

} // namespace iw3
