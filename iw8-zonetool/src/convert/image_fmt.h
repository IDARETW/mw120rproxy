// image_fmt.h — the IMAGE-family A<->B interchange + IW3->IW8 conversion helpers.
// Owned by the image family (src/{iw3,iw8,convert}/*image*). Namespaced `cvtimg` to avoid clashes
// with other families' helper headers.
//
// WHAT THIS DEFINES
//   1. ImageBlob       — the self-describing image payload (SPEC §3a) the Stage-A reader writes and the
//                        Stage-B writer reads, via ZoneSource::addImage/getImage (raw .iwi-slot bytes).
//                        It is "IWi-like": a fixed metadata header (carrying the IW8-relevant GfxImage
//                        fields) + the raw DXT/RGBA pixel mip-chain copied verbatim from the IW3
//                        GfxImageLoadDef.data[]. Self-describing magic "ZTIMG\0" + version.
//   2. Iw3IwiFormat    — the IW3 IWi format codes (from zonetool IW3/IW4 IGfxImage), the FROM side.
//   3. iw3_to_iw8_pixel_format() — map an IW3 IWi format code to an IW8 GfxPixelFormat enum value.
//                        ⚠ The exact IW8 GfxPixelFormat ordinals are NOT resolvable offline (need
//                        dev.i64; SPEC §4c). The mapping below uses the documented IW-line ordinals and
//                        is the ONE image field flagged for the integrator/verifier to confirm against a
//                        real mp_shipment.ff GfxImage instance.
//   4. iw3_format helpers — bytes-per-block / block geometry so the reader/writer can sanity the pixel
//                        size (and the writer can fill GfxImage.totalSize when the IW3 size is absent).
#pragma once
#include <cstdint>
#include <cstddef>

namespace cvtimg {

// ---- self-describing image blob (the images/<name>.iwi payload) ---------------------------------
// Stored via ZoneSource::addImage(name, bytes) where bytes = [ImageBlobHeader][origName NUL][pixels].
// The magic + version make it self-describing per SPEC §3a (we do NOT reuse ZoneSource::wrapBlob here
// because we need the typed metadata header inline; the magic still self-identifies it).
//
// NAME POLICY: the on-disk file + manifest key use a filesystem-SAFE stem (map images contain '*'),
// but the REAL asset name (e.g. "*lightmap0") is what the IW8 GfxImage must carry so material texture
// references resolve. We therefore embed the original name (length-prefixed, right after the header)
// so the writer emits the true name regardless of the file stem.
#pragma pack(push, 1)
struct ImageBlobHeader {
    char     magic[6];        // "ZTIMG\0"
    uint16_t version;         // = kImageBlobVersion
    // --- source (IW3) identity ---
    int32_t  iw3Format;       // IW3 IWi format code (Iw3IwiFormat) — the FROM format
    uint8_t  mapType;         // IW3 MapType (3=2D,5=cube,...) -> IW8 mapType
    uint8_t  semantic;        // IW3 semantic (material overrides; default carried)
    uint8_t  category;        // IW3 category
    uint8_t  levelCount;      // mip levels (>=1)
    // --- geometry ---
    uint16_t width;
    uint16_t height;
    uint16_t depth;
    uint16_t numElements;     // array size (1 for plain 2D; 6-faces => mapType cube)
    uint32_t pixelSize;       // bytes of pixel data that follow (after the name) — the mip chain
    uint32_t flags;           // IW3 image flags (carried, mostly informational for the prototype)
    uint16_t nameLen;         // bytes of the original-name string that follows (incl. NUL)
    uint8_t  _reserved[6];    // future use; zero
};
#pragma pack(pop)
// Blob layout: [ImageBlobHeader][origName (nameLen bytes, NUL-terminated)][pixels (pixelSize bytes)].
static_assert(sizeof(ImageBlobHeader) == 40, "ImageBlobHeader must be 40 bytes (self-describing)");

static constexpr uint16_t kImageBlobVersion = 1;
static constexpr char     kImageBlobMagic[6] = { 'Z','T','I','M','G','\0' };

// ---- IW3 IWi format codes (zonetool IW3/IW4 IGfxImage) ------------------------------------------
// These are the values found in a linked-zone IW3 GfxImageLoadDef.format AND the IWi-file format byte.
enum Iw3IwiFormat : int32_t {
    IWI_INVALID = 0,
    IWI_ARGB32  = 1,   // R8G8B8A8 (32bpp)
    IWI_RGB24   = 2,   // R8G8B8   (24bpp)
    IWI_GA16    = 3,   // A8L8 / grey+alpha (16bpp)
    IWI_A8      = 4,   // alpha-only (8bpp)
    IWI_DXT1    = 11,  // BC1
    IWI_DXT3    = 12,  // BC2
    IWI_DXT5    = 13,  // BC3
};

// ---- IW8 GfxPixelFormat (PROVISIONAL — see ⚠ above) ---------------------------------------------
// The exact IW8 enum is unresolved offline. We expose a small, documented set of IW-line ordinals.
// The mapping picks the format that matches the source block-compression / channel layout. If the
// integrator confirms different ordinals from dev.i64 / a real instance, ONLY this table changes.
enum Iw8PixelFormatProvisional : uint32_t {
    // IW-line common ordinals (e.g. used across the post-IW6 GfxPixelFormat enum). PROVISIONAL.
    IW8_FMT_R8G8B8A8_UNORM      = 1,
    IW8_FMT_BC1_UNORM           = 32,   // DXT1
    IW8_FMT_BC2_UNORM           = 35,   // DXT3
    IW8_FMT_BC3_UNORM           = 36,   // DXT5
    IW8_FMT_BC7_UNORM           = 50,
};

// Map an IW3 IWi format -> an IW8 GfxPixelFormat (provisional). Unknown -> R8G8B8A8.
inline uint32_t iw3_to_iw8_pixel_format(int32_t iw3Format) {
    switch (iw3Format) {
        case IWI_DXT1:   return IW8_FMT_BC1_UNORM;
        case IWI_DXT3:   return IW8_FMT_BC2_UNORM;
        case IWI_DXT5:   return IW8_FMT_BC3_UNORM;
        case IWI_ARGB32: return IW8_FMT_R8G8B8A8_UNORM;
        case IWI_RGB24:  return IW8_FMT_R8G8B8A8_UNORM; // 24bpp has no direct GPU format -> treat as 32
        case IWI_GA16:   return IW8_FMT_R8G8B8A8_UNORM;
        case IWI_A8:     return IW8_FMT_R8G8B8A8_UNORM;
        default:         return IW8_FMT_R8G8B8A8_UNORM;
    }
}

// True if the IW3 format is a block-compressed (DXT/BC) format.
inline bool iw3_is_block_compressed(int32_t iw3Format) {
    return iw3Format == IWI_DXT1 || iw3Format == IWI_DXT3 || iw3Format == IWI_DXT5;
}

// Bytes per 4x4 block (DXT1=8, DXT3/5=16) for block-compressed formats; 0 for uncompressed.
inline uint32_t iw3_block_bytes(int32_t iw3Format) {
    switch (iw3Format) {
        case IWI_DXT1: return 8;
        case IWI_DXT3: return 16;
        case IWI_DXT5: return 16;
        default:       return 0;
    }
}

// Bytes per pixel for uncompressed formats (0 for block-compressed).
inline uint32_t iw3_bytes_per_pixel(int32_t iw3Format) {
    switch (iw3Format) {
        case IWI_ARGB32: return 4;
        case IWI_RGB24:  return 3;
        case IWI_GA16:   return 2;
        case IWI_A8:     return 1;
        default:         return 0;
    }
}

// Compute the byte size of a single mip level (level 0 = full size) for the given format/dims.
inline uint32_t iw3_level_size(int32_t fmt, uint32_t w, uint32_t h) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (iw3_is_block_compressed(fmt)) {
        uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        return bw * bh * iw3_block_bytes(fmt);
    }
    uint32_t bpp = iw3_bytes_per_pixel(fmt);
    return w * h * (bpp ? bpp : 4);
}

// Sum all mip levels (levelCount) of a 2D image. Used to cross-check the IW3 resourceSize and to fill
// the IW8 totalSize when the source size is unavailable.
inline uint32_t iw3_total_size(int32_t fmt, uint32_t w, uint32_t h, uint32_t levels) {
    uint32_t total = 0;
    if (levels == 0) levels = 1;
    for (uint32_t i = 0; i < levels; ++i) {
        total += iw3_level_size(fmt, w >> i, h >> i);
    }
    return total;
}

struct ImageBlobHeader;  // fwd (defined above)
// Validate (and if needed repair) the pixel size in a blob header against format+dims, clamped to the
// bytes actually present. Defined in convert/image.cpp. Returns the size the writer should copy/declare.
uint32_t validate_pixel_size(const ImageBlobHeader& h, uint32_t availableBytes);

} // namespace cvtimg
