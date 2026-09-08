// iw3_zone.h — the OFFLINE IW3 (CoD4) zone loader framework (SPEC §2b).
// Reproduces DB_LoadXFile *from inflated zone bytes* (NO game memory): bump-allocate each stream,
// walk the XAsset list, dispatch each supported asset to a registered Load_<Type>. Implements the IW3
// 32-bit tagged-pointer convention (0=null, -1=follows-inline, else packed Offset{value:28,stream:4}).
//
// The per-asset Load_<Type> readers live in src/iw3/*.cpp and register themselves via the extern
// dispatch table declared here. The skeleton ships a default registration of no-op stubs.
#pragma once
#include "iw3_structs.h"
#include <cstdint>
#include <string>
#include <vector>

namespace zt { struct Iw3Fastfile; }
namespace iw3sr { class ZoneSource; } // fwd; the real type is src/zonesrc/zone_source.h

namespace iw3 {

// IW3 (CoD4) XFile block — the FRONT of the inflated zone (NO magic/version; those are the on-disk
// .ff header, consumed by ff_io before inflate). EMPIRICALLY CONFIRMED from the real decompressed
// cod4builds/mp_test.ff + mp_test_load.ff: 44 bytes = { u32 size; u32 externalSize; u32 blockSize[9] }
// (CoD4 has 9 memory streams). The XAssetList follows immediately at offset 44 (start of stream 0).
//   mp_test_load: size=0x311 ext=0xAAAB8 block=[0xA4,0,0,0,0xB3,0,0,0,0]  -> XAssetList{0,0,6,-1}
//   mp_test:      size=0x25DE15A ext=0x75E8C80 block=[0x35C04A,...]       -> XAssetList{143,-1,436,-1}
#pragma pack(push, 4)
struct Iw3XFileHeader {
    uint32_t size;          // total in-zone data size (after this header)
    uint32_t externalSize;  // external/runtime size
    uint32_t blockSize[9];  // per-stream sizes; the inflated blob after the header = streams 0..8 concat
};
#pragma pack(pop)
static_assert(sizeof(Iw3XFileHeader) == 44, "IW3 XFile header is 44 bytes");

// Number of IW3 zone streams (CoD4 = 9). Used to compute the packed-offset shift (Offset.stream:4).
static constexpr int kIw3NumStreams = 9;

// IW3 (CoD4) serialized XAssetList root (16 bytes), at offset sizeof(Iw3XFileHeader). 32-bit ptrs.
#pragma pack(push, 4)
struct Iw3XAssetList {
    uint32_t scriptStringCount; // 0x00
    uint32_t scriptStrings;     // 0x04  const char** (0 / -1 follows)
    uint32_t assetCount;        // 0x08
    uint32_t assets;            // 0x0C  XAsset* (-1 follows -> the contiguous XAsset[assetCount] array)
};
#pragma pack(pop)
static_assert(sizeof(Iw3XAssetList) == 16, "IW3 XAssetList is 16 bytes");

// IW3 (CoD4) serialized XAsset (8 bytes): { int type; u32 ptr }.
#pragma pack(push, 4)
struct Iw3XAssetEntry { int32_t type; uint32_t ptr; };
#pragma pack(pop)
static_assert(sizeof(Iw3XAssetEntry) == 8, "IW3 XAsset is 8 bytes");

// ---- load context ---------------------------------------------------------------------------------
// A read cursor over the inflated zone, with the IW3 pointer-fixup helpers (inverse of the
// zonetool-develop ZoneBuffer writer). Per-asset readers consume it.
class LoadCtx {
public:
    explicit LoadCtx(const std::vector<uint8_t>& zone) : zone_(zone) {}

    // Sequential read from the current cursor (the loader reads inline data front-to-back).
    bool read(void* dst, size_t n);
    template <typename T> bool read(T& v) { return read(&v, sizeof(T)); }

    size_t pos() const { return pos_; }
    void   seek(size_t p) { pos_ = p; }
    size_t size() const { return zone_.size(); }
    const uint8_t* base() const { return zone_.data(); }

    // Resolve a serialized 32-bit pointer value to an absolute zone byte-offset (or npos).
    //   v == 0           -> null      (returns npos, *follows=false)
    //   v == 0xFFFFFFFF  -> follows   (returns current cursor, *follows=true)
    //   else             -> packed Offset{value:28,stream:4}: stored as (value+1); we subtract 1 and
    //                       map (stream,value) to an absolute offset via the stream base table.
    static constexpr size_t npos = static_cast<size_t>(-1);
    size_t resolve(uint32_t v, bool* follows = nullptr) const;

    // Stream-base table: streamBase_[i] = absolute offset where stream i begins in the blob.
    void setStreamBases(const std::vector<size_t>& bases) { streamBase_ = bases; }

private:
    const std::vector<uint8_t>& zone_;
    size_t                      pos_ = 0;
    std::vector<size_t>         streamBase_;
};

// ---- asset dispatch -------------------------------------------------------------------------------
// IW3 XAssetType ordinals we dispatch (CoD4 enum subset; the FOCUS families).
// CONFIRMED from zonetool-develop/src/IW3/Structs.hpp:15 (CoD4 XAssetType). IW3 has NO standalone
// xmodelsurfs asset — surfaces live inside xmodel (xmodelpieces=0); XMODELSURFS aliases 0 so existing
// registrations still link (harmless — type 0 doesn't appear standalone in shipped map zones).
enum Iw3AssetType : int32_t {
    IW3_ASSET_XMODELPIECES = 0,
    IW3_ASSET_XMODELSURFS  = 0,    // alias (no standalone surfs asset in IW3)
    IW3_ASSET_PHYSPRESET   = 1,
    IW3_ASSET_XANIMPARTS   = 2,
    IW3_ASSET_XMODEL       = 3,
    IW3_ASSET_MATERIAL     = 4,
    IW3_ASSET_TECHSET      = 5,
    IW3_ASSET_IMAGE        = 6,
    IW3_ASSET_SOUND        = 7,
    IW3_ASSET_SNDCURVE     = 8,
    IW3_ASSET_LOADED_SOUND = 9,
    IW3_ASSET_CLIPMAP_SP   = 10,
    IW3_ASSET_CLIPMAP      = 11,   // col_map_mp (clipMap_t)
    IW3_ASSET_CLIPMAP_PVS  = 10,   // alias -> col_map_sp (no PVS type in IW3)
    IW3_ASSET_COMWORLD     = 12,   // com_map
    IW3_ASSET_GAMEWORLDSP  = 13,
    IW3_ASSET_GAMEWORLDMP  = 14,
    IW3_ASSET_MAPENTS      = 15,
    IW3_ASSET_GFXWORLD     = 16,   // gfx_map
    IW3_ASSET_LIGHTDEF     = 17,
    IW3_ASSET_RAWFILE      = 31,
    IW3_ASSET_STRINGTABLE  = 32,
    IW3_ASSET_COUNT        = 33,
};

// IW3 asset-type ordinal -> short IW3 name (for the zone report). Returns "type<N>" if unknown.
const char* iw3TypeName(int t);
// IW3 asset-type ordinal -> IW8 zone-source type name (SPEC §3), or nullptr if it has no IW8 mapping.
const char* iw3ToIw8TypeName(int t);

// A registered per-type reader: given the load ctx (cursor at the asset header) and the zone-source to
// dump into, read the IW3 struct + its pointer graph and write the zone-source files. Returns false on
// a hard error (the loader logs + stops). The skeleton stubs just log "TODO <fam>".
using LoadFn = bool(*)(LoadCtx& lc, iw3sr::ZoneSource& zs);

// Register a reader for an IW3 asset type (called from per-asset .cpp module init, or the stub init).
void registerLoad(int iw3Type, LoadFn fn);
LoadFn getLoad(int iw3Type);

// Install the default no-op stub readers (used until per-asset modules override them).
void installStubLoaders();

// ---- top-level driver -----------------------------------------------------------------------------
// Walk an inflated IW3 zone: read the XFileHeader, set up stream bases, read the asset list, dispatch
// each supported asset to its Load_<Type> (dumping into `zs`). Unsupported types are skipped (logged).
// Returns false on a fatal parse error. `mapName` seeds the zone-source manifest name.
bool loadZone(const zt::Iw3Fastfile& ff, iw3sr::ZoneSource& zs, const std::string& mapName);

} // namespace iw3
