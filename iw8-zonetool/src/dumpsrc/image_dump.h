// image_dump.h — IMAGE-family (Tier2) DUMP-PATH interchange + entry points.
// =================================================================================================
// Owned by the image family (src/dumpsrc/read_image.cpp + src/iw8/write_image.cpp +
// src/convert/conv_image.cpp). Namespaced `dumpimg` to stay disjoint from the LEGACY IW3-.ff image
// path (convert::iw8_write_image / cvtimg::ImageBlobHeader in src/{iw8,convert}/asset_image.cpp,
// image_fmt.h). This is the NEW pipeline: ZoneTool dump folder (images/<name>.ffImg) -> IW8 GfxImage.
//
// PIPELINE (3 stages, one per owned .cpp):
//   read_image.cpp   :  parse images/<cleanName>.ffImg (raw FileReader stream, NO BinaryDumper tags)
//                       per the AUTHORITATIVE IW5 IGfxImage::dump() byte order -> ImageDumpFile.
//   conv_image.cpp   :  map the IW5 image fields -> IW8 GfxImage fields                 -> Iw8ImageDef.
//   write_image.cpp  :  serialize an Iw8ImageDef into the ZoneWriter (GfxImage 0xE8 + -2/null/streams
//                       sentinels + inline resident pixel buffer). Also the standalone-zone helper.
//
// FLAG-GATED: all of this is emitted ONLY when main runs with --assets. It can NOT touch the Tier1
// srv/com map zone (separate ZoneBuffer, separate call site), so it can never break the load.
//
// ============================ ON-DISK .ffImg FORMAT (authority) ===================================
// reference/IW5_DUMP_FORMAT.md §9 + zonetool-develop/src/IW5/Assets/GfxImage.cpp dump() (lines 408-429).
// images/<cleanName>.ffImg is a HAND-ROLLED little-endian stream (NO DUMP_TYPE tag bytes), written iff
// the source image had texture && texture->dataSize. Byte order (confirmed vs the 4 real mp_test dumps):
//     char  mapType            (1)   // 3=2D, 4=3D, 5=cube
//     char  semantic           (1)
//     char  category           (1)
//     char  flags              (1)
//     int   cardMemory         (4)
//     int   dataLen1           (4)
//     int   dataLen2           (4)
//     int   height             (4)   // dump casts the short live fields through int
//     int   width              (4)
//     int   depth              (4)
//     char* name               -> NUL-terminated C-string  (the TRUE asset name, may contain '*')
//   GfxImageLoadDef:
//     char  mipLevels          (1)
//     char  ldFlags            (1)
//     int   dimensions[0]      (4)
//     int   dimensions[1]      (4)
//     int   dimensions[2]      (4)
//     int   format             (4)   // IW5 GfxImageLoadDef.format ("compression magic")
//     int   dataSize           (4)
//     <dataSize bytes of raw pixels>
//
// REALITY (verified on all 4 mp_test dumps): for these MAP images the LoadDef block (mipLevels/ldFlags/
// dimensions/format/dataSize) holds STALE heap bytes — e.g. mipLevels=156, format=50, dataSize=3 — and
// dataSize is only 3/21 bytes (NOT a real mip chain). The RELIABLE geometry is in the HEADER
// (height/width/depth/dataLen1/dataLen2) which decode cleanly (512x512, 1024x1024, 64x64 cube). So the
// converter trusts the header dims and treats the LoadDef.format/dataSize as best-effort only — the
// emitted IW8 GfxImage is structurally valid + resident with a tiny inline pixel buffer (the dump gives
// no usable full-res texture for these map sidecars). This is acceptable: image(19) is Tier2/flag-gated.
// =================================================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpimg {

// IW5 GfxImage.mapType (zonetool IW5 Structs.hpp: 5=cube, 4=3d, 3=2d).
enum Iw5MapType : uint8_t { IW5_MAPTYPE_2D = 3, IW5_MAPTYPE_3D = 4, IW5_MAPTYPE_CUBE = 5 };

// ---- Stage A result: the parsed .ffImg (IW5 field set, verbatim) --------------------------------
struct ImageDumpFile {
    bool        loaded = false;       // header+name parsed and the file was well-formed
    std::string name;                 // TRUE asset name from the file (may contain '*', '$')
    std::string fileStem;             // the on-disk cleaned stem (no '*') the file was found under

    // header block
    uint8_t  mapType   = IW5_MAPTYPE_2D;
    uint8_t  semantic  = 0;
    uint8_t  category  = 0;
    uint8_t  flags     = 0;
    int32_t  cardMemory = 0;
    int32_t  dataLen1  = 0;
    int32_t  dataLen2  = 0;
    int32_t  height    = 0;
    int32_t  width     = 0;
    int32_t  depth     = 0;

    // GfxImageLoadDef block (STALE for map sidecars — carried best-effort)
    uint8_t  mipLevels = 0;
    uint8_t  ldFlags   = 0;
    int32_t  dimensions[3] = {0,0,0};
    int32_t  format    = 0;           // IW5 LoadDef format ("compression magic"); often stale here
    int32_t  dataSize  = 0;           // bytes of pixel payload that follow
    bool     loadDefStale = false;    // heuristic: the LoadDef looked like stale heap (see read_image.cpp)

    std::vector<uint8_t> pixels;      // dataSize bytes (may be a tiny stale blob for map sidecars)
};

// ---- Stage B input: the IW8-mapped GfxImage def the writer serializes ---------------------------
// Plain field values (NOT a zone struct) — write_image.cpp stamps them into the fixed 0xE8 buffer.
struct Iw8ImageDef {
    std::string name;                 // GfxImage.name (true asset name)

    uint32_t format    = 0;           // GfxPixelFormat (IW8 enum; provisional map — see conv_image.cpp)
    uint32_t flags     = 0;           // GfxImageFlags
    uint32_t totalSize = 0;           // GfxImage.totalSize (bytes; = pixel payload, or dims-derived)
    uint16_t width     = 0;
    uint16_t height    = 0;
    uint16_t depth     = 1;
    uint16_t numElements = 1;         // 6 for cube
    uint8_t  semantic  = 0;           // TextureSemantic
    uint8_t  category  = 0;           // GfxImageCategory
    uint8_t  levelCount = 1;
    uint8_t  streamedPartCount = 0;   // 0 => fully resident (no xpak)

    std::vector<uint8_t> pixels;      // inline resident pixels (may be empty / tiny)
};

// ============================= ENTRY POINTS (one per owned .cpp) ==================================

// Stage A (read_image.cpp): parse images/<cleanName>.ffImg under <dumpDir>. `nameOrStem` may be the
// TRUE asset name ("*lightmap0_primary") or the on-disk stem ("_lightmap0_primary") — either resolves.
// Returns loaded=false (with a warning) if the file is missing or malformed.
ImageDumpFile readImageDump(const std::string& dumpDir, const std::string& nameOrStem);

// Convenience: clean a TRUE asset name to its on-disk .ffImg stem (IW5 ClearAssetName: '*' -> '_').
std::string cleanImageName(const std::string& name);

// Enumerate the .ffImg files present under <dumpDir>/images. Returns the on-disk stems (no extension).
std::vector<std::string> listImageDumps(const std::string& dumpDir);

// Stage B-convert (conv_image.cpp): map the parsed IW5 fields -> the IW8 GfxImage def. Pure; no IO.
Iw8ImageDef convertImage(const ImageDumpFile& in);

} // namespace dumpimg

// ---- Stage B-write (write_image.cpp): serialize into the ZoneWriter. Declared in iw8 namespace so it
// sits next to the other iw8 writers; defined in src/iw8/write_image.cpp. ------------------------------
namespace iw8 { class ZoneWriter; class ZoneBuffer; }
namespace dumpimg {

// Emit ONE GfxImage(19) asset body into an already-open ZoneWriter body callback (the struct ->
// TEMP_PRELOAD, name + inline pixels -> VIRTUAL). The caller frames the XAsset entry (type=19,
// header=-3). Mirrors the proven map_zone.h emit pattern. Returns false (logs) on a bad def.
bool emitImageBody(iw8::ZoneWriter& zw, const Iw8ImageDef& def);

// Register the image as a full top-level asset on a ZoneWriter (adds the XAsset(19) entry whose body
// calls emitImageBody). Use this from the --assets main-zone assembly.
bool addImageAsset(iw8::ZoneWriter& zw, const Iw8ImageDef& def);

// Build a STANDALONE valid image-only zone (XAssetList + N x image(19)) into `zb`, for an isolated
// --assets test / container validation. Returns the asset count emitted.
size_t buildImageZone(iw8::ZoneBuffer& zb, const std::vector<Iw8ImageDef>& defs);

} // namespace dumpimg
