#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpimg
{

// IW5 GfxImage.mapType (zonetool IW5 Structs.hpp: 5=cube, 4=3d, 3=2d).
enum Iw5MapType : uint8_t
{
    IW5_MAPTYPE_2D = 3,
    IW5_MAPTYPE_3D = 4,
    IW5_MAPTYPE_CUBE = 5
};

// ---- Stage A result: a parsed .dds, .ffImg, or .iwi image ---------------------------------------
struct ImageDumpFile
{
    bool loaded = false;  // header+name parsed and the file was well-formed
    std::string name;     // TRUE asset name from the file (may contain '*', '$')
    std::string fileStem; // the on-disk cleaned stem (no '*') the file was found under

    // header block
    uint8_t mapType = IW5_MAPTYPE_2D;
    uint8_t semantic = 0;
    uint8_t category = 0;
    uint8_t flags = 0;
    int32_t cardMemory = 0;
    int32_t dataLen1 = 0;
    int32_t dataLen2 = 0;
    int32_t height = 0;
    int32_t width = 0;
    int32_t depth = 0;

    // GfxImageLoadDef block (STALE for map sidecars — carried best-effort)
    uint8_t mipLevels = 0;
    uint8_t ldFlags = 0;
    int32_t dimensions[3] = {0, 0, 0};
    int32_t format = 0;        // IW5 LoadDef format ("compression magic"); often stale here
    int32_t dataSize = 0;      // bytes of pixel payload that follow
    bool loadDefStale = false; // heuristic: the LoadDef looked like stale heap (see read_image.cpp)

    std::vector<uint8_t> pixels; // dataSize bytes (may be a tiny stale blob for map sidecars)
};

// ---- Stage B input: the IW8-mapped GfxImage def the writer serializes ---------------------------
// Plain field values (NOT a zone struct) — write_image.cpp stamps them into the fixed 0xE8 buffer.
struct Iw8ImageDef
{
    std::string name; // GfxImage.name (true asset name)

    uint32_t format = 0;    // Replay GfxPixelFormat
    uint32_t flags = 0;     // GfxImageFlags
    uint32_t totalSize = 0; // GfxImage.totalSize (bytes; = pixel payload, or dims-derived)
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t depth = 1;
    uint16_t numElements = 1; // 6 for cube
    uint8_t semantic = 0;     // TextureSemantic
    uint8_t category = 0;     // GfxImageCategory
    uint8_t levelCount = 1;
    uint8_t streamedPartCount = 0; // 0 => fully resident (no xpak)

    std::vector<uint8_t> pixels; // inline resident pixels (may be empty / tiny)
};

// ============================= ENTRY POINTS (one per owned .cpp)
// ==================================

// Stage A (read_image.cpp): parse an image under <dumpDir>/images. `nameOrStem` may be the
// TRUE asset name ("*lightmap0_primary") or the on-disk stem ("_lightmap0_primary") — either
// resolves. Returns loaded=false (with a warning) if the file is missing or malformed.
ImageDumpFile readImageDump(const std::string &dumpDir, const std::string &nameOrStem);

// Convenience: clean a TRUE asset name to its on-disk .ffImg stem (IW5 ClearAssetName: '*' -> '_').
std::string cleanImageName(const std::string &name);

// Enumerate supported image files under <dumpDir>/images. Returns the on-disk stems (no
// extension).
std::vector<std::string> listImageDumps(const std::string &dumpDir);

// Stage B-convert (conv_image.cpp): map the parsed IW5 fields -> the IW8 GfxImage def. Pure; no IO.
Iw8ImageDef convertImage(const ImageDumpFile &in);

} // namespace dumpimg

// ---- Stage B-write (write_image.cpp): serialize into the ZoneWriter. Declared in iw8 namespace so
// it sits next to the other iw8 writers; defined in src/iw8/write_image.cpp.
// ------------------------------
namespace iw8
{
class ZoneWriter;
}
namespace dumpimg
{

// Emit ONE GfxImage(19) asset body into an already-open ZoneWriter body callback (the struct ->
// TEMP_PRELOAD, name + inline pixels -> VIRTUAL). The caller frames the XAsset entry (type=19,
// header=-3). Mirrors the proven map_zone.h emit pattern. Returns false (logs) on a bad def.
bool emitImageBody(iw8::ZoneWriter &zw, const Iw8ImageDef &def);

// Register the image as a full top-level asset on a ZoneWriter (adds the XAsset(19) entry whose
// body calls emitImageBody). Use this from the build-map main-zone assembly.
bool addImageAsset(iw8::ZoneWriter &zw, const Iw8ImageDef &def);

} // namespace dumpimg
