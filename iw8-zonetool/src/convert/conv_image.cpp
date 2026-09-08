// conv_image.cpp — Stage-B convert: map a parsed IW5 .ffImg (ImageDumpFile) -> IW8 GfxImage def.
// =================================================================================================
// Pure field mapping, no IO. The IW8 GfxImage(19)=0xE8 layout is built fresh (write_image.cpp); here we
// only translate the IW5 image fields to the IW8-relevant values.
//
// FORMAT (the one genuinely unresolved field, SPEC §4c "image format ordinal not resolvable offline").
//   The IW5 GfxImageLoadDef.format is a D3D "compression magic": either a DXT FourCC ('DXT1'/'DXT3'/
//   'DXT5' = 0x31..0x35 555844) OR a small numeric D3DFORMAT-ish code. The IW8 GfxPixelFormat enum
//   ordinals cannot be pinned offline, so we map to the PROVISIONAL IW-line ordinals (cvtimg, image_fmt.h)
//   and flag this as the single value the integrator/verifier confirms against a real mp_shipment.ff
//   GfxImage instance. For mp_test's map sidecars the LoadDef is STALE (read_image.cpp sets
//   loadDefStale) so the format code is unreliable -> we fall back to an UNCOMPRESSED RGBA8 format
//   (the safe, dims-derivable default), which is correct for the lightmap/$outdoor/reflection sidecars
//   (they are not DXT surfaces in the dump).
//
// SEMANTIC / CATEGORY: carried verbatim from the IW5 dump (they are 1-byte enums shared across the
// IW-line: semantic ~ TextureSemantic, category ~ GfxImageCategory). When the dump value is 0 we apply
// a sane default (COLOR_MAP / LOAD_FROM_FILE) so the loader has a defined semantic.
//
// SIZE / RESIDENCY: the prototype emits the image FULLY RESIDENT (streamedPartCount=0, streams[4]
// zeroed) with the dump's pixel payload inline (write_image.cpp). totalSize = the inline pixel byte
// count when present; otherwise a dims-derived estimate (so the field is non-zero/defined). For the
// stale map sidecars the inline payload is tiny (3/21 B) — structurally valid, not a usable texture.
//
// FILE OWNERSHIP: image family (read_image.cpp / conv_image.cpp / write_image.cpp / image_dump.h).
#include "../dumpsrc/image_dump.h"
#include "image_fmt.h"           // cvtimg:: provisional IW8 pixel-format ordinals + size helpers (reuse)
#include "../common/log.h"
#include <cstdint>

namespace dumpimg {

using namespace zt;

namespace {

// IW8 default enum values (IW-line; same as the legacy IW3-path defaults in asset_image.cpp).
constexpr uint8_t IW8_IMG_CATEGORY_LOAD_FROM_FILE = 3;   // GfxImageCategory
constexpr uint8_t IW8_TS_COLOR_MAP                = 2;   // TextureSemantic (2d color)

// DXT FourCC magics found in an IW5 GfxImageLoadDef.format ("DXT1"/"DXT3"/"DXT5").
constexpr int32_t FOURCC_DXT1 = 0x31545844; // 'DXT1'
constexpr int32_t FOURCC_DXT3 = 0x33545844; // 'DXT3'
constexpr int32_t FOURCC_DXT5 = 0x35545844; // 'DXT5'

// Map the IW5 LoadDef.format (FourCC or small D3DFORMAT code) -> our provisional IW8 GfxPixelFormat.
// Returns true and sets `iw8fmt`/`isBlock` when recognised; false when the code is unusable (stale).
bool mapIw5Format(int32_t iw5fmt, uint32_t& iw8fmt, bool& isBlock) {
    isBlock = false;
    switch (iw5fmt) {
        case FOURCC_DXT1: iw8fmt = cvtimg::IW8_FMT_BC1_UNORM;     isBlock = true; return true;
        case FOURCC_DXT3: iw8fmt = cvtimg::IW8_FMT_BC2_UNORM;     isBlock = true; return true;
        case FOURCC_DXT5: iw8fmt = cvtimg::IW8_FMT_BC3_UNORM;     isBlock = true; return true;
        // small numeric codes seen in dumps that clearly denote an uncompressed surface
        case 20: case 21:  // RGB / ARGB-ish (D3DFMT_R8G8B8 / A8R8G8B8 family)
            iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM; return true;
        default:
            return false;  // unrecognised / stale
    }
}

// Bytes for a single uncompressed RGBA8 mip level.
uint32_t rgba8LevelBytes(uint32_t w, uint32_t h) {
    if (!w) w = 1; if (!h) h = 1;
    return w * h * 4u;
}

} // namespace

Iw8ImageDef convertImage(const ImageDumpFile& in) {
    Iw8ImageDef out;
    out.name = in.name;

    // ---- geometry (HEADER fields — reliable even when the LoadDef is stale) ----
    out.width  = (uint16_t)(in.width  > 0 ? in.width  : 0);
    out.height = (uint16_t)(in.height > 0 ? in.height : 0);
    out.depth  = (uint16_t)(in.depth  > 0 ? in.depth  : 1);
    out.numElements = (in.mapType == IW5_MAPTYPE_CUBE) ? 6 : 1;

    // ---- semantic / category (carried; sane defaults when zero) ----
    out.semantic = in.semantic ? in.semantic : IW8_TS_COLOR_MAP;
    out.category = in.category ? in.category : IW8_IMG_CATEGORY_LOAD_FROM_FILE;

    // ---- level count (LoadDef mipLevels is stale here -> default 1 unless plainly valid) ----
    out.levelCount = (in.mipLevels >= 1 && in.mipLevels <= 16 && !in.loadDefStale) ? in.mipLevels : 1;

    // ---- format (the flagged-unresolved field) ----
    uint32_t iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM;
    bool isBlock = false;
    if (in.loadDefStale || !mapIw5Format(in.format, iw8fmt, isBlock)) {
        // stale or unrecognised LoadDef -> safe uncompressed default (dims-derivable).
        iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM;
        isBlock = false;
        if (!in.loadDefStale)
            warn("dumpimg: '%s' unrecognised LoadDef format %d -> RGBA8 (provisional)",
                 in.name.c_str(), in.format);
    }
    out.format = iw8fmt;

    // ---- flags (IW8 GfxImageFlags). Resident map sidecar: no streaming flags. Carry nothing risky. ----
    out.flags = 0;
    out.streamedPartCount = 0;  // fully resident — no xpak dependency we can't satisfy offline

    // ---- pixels (inline resident) + totalSize ----
    out.pixels = in.pixels;                       // verbatim dump payload (tiny for map sidecars)
    if (!out.pixels.empty()) {
        out.totalSize = (uint32_t)out.pixels.size();
    } else {
        // no usable payload: declare a dims-derived totalSize so the field is defined (non-zero).
        // (We do NOT fabricate pixels — the writer keeps pixels=null when empty.)
        if (isBlock) {
            out.totalSize = cvtimg::iw3_total_size(
                (in.format == FOURCC_DXT1) ? cvtimg::IWI_DXT1
                : (in.format == FOURCC_DXT3) ? cvtimg::IWI_DXT3 : cvtimg::IWI_DXT5,
                out.width, out.height, out.levelCount);
        } else {
            out.totalSize = rgba8LevelBytes(out.width, out.height);
        }
    }

    info("dumpimg: convert '%s' -> IW8 %ux%u d=%u elem=%u fmt=%u sem=%u cat=%u L=%u resident px=%zu totalSize=%u",
         out.name.c_str(), out.width, out.height, out.depth, out.numElements, out.format,
         out.semantic, out.category, out.levelCount, out.pixels.size(), out.totalSize);
    return out;
}

} // namespace dumpimg
