

#include "../convert/registry.h"
#include "../convert/image_fmt.h"
#include "../common/log.h"
#include "iw8_zone.h"
#include "iw8_structs.h"
#include "iw8_focus_structs.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <string>
#include <vector>

namespace convert {

// GfxImage(19) = 0xE8 bytes (g_assetSizes). Pinned field offsets (iw8_focus::GfxImage):
static constexpr size_t kImageSize = iw8sz::IMAGE; // 0xE8
static constexpr size_t kGI_name = 0x00;           // const char*   //PTR
static constexpr size_t kGI_packedAtlas = 0x08;    // uint8_t*      //PTR
static constexpr size_t kGI_textureId = 0x10;      // u32 (runtime; 0)
static constexpr size_t kGI_format = 0x14;         // u32 GfxPixelFormat
static constexpr size_t kGI_flags = 0x18;          // u32 GfxImageFlags
static constexpr size_t kGI_totalSize = 0x1C;      // u32
static constexpr size_t kGI_semanticSpec = 0x20;   // u32
static constexpr size_t kGI_width = 0x24;          // u16
static constexpr size_t kGI_height = 0x26;         // u16
static constexpr size_t kGI_depth = 0x28;          // u16
static constexpr size_t kGI_numElements = 0x2A;    // u16
static constexpr size_t kGI_atlasInfo = 0x2C;      // u16
static constexpr size_t kGI_semantic = 0x2E;       // u8
static constexpr size_t kGI_category = 0x2F;       // u8
static constexpr size_t kGI_levelCount = 0x30;     // u8
static constexpr size_t kGI_streamedPart = 0x31;   // u8
static constexpr size_t kGI_decalAtlasIdx = 0x32;  // u8
static constexpr size_t kGI_freqBias = 0x33;       // i8
static constexpr size_t kGI_streams = 0x38;        // GfxImageStreamData[4] (0xA0) — zeroed
static constexpr size_t kGI_fallback = 0xD8;       // GfxImageFallback* //PTR
static constexpr size_t kGI_pixels = 0xE0;         // void* GfxImagePixels //PTR

// keep the pinned struct in lockstep with these literal offsets (compile-time guard)
static_assert(sizeof(iw8_focus::GfxImage) == kImageSize, "GfxImage must be 0xE8");
static_assert(offsetof(iw8_focus::GfxImage, format) == kGI_format, "format@0x14");
static_assert(offsetof(iw8_focus::GfxImage, totalSize) == kGI_totalSize, "totalSize@0x1C");
static_assert(offsetof(iw8_focus::GfxImage, streams) == kGI_streams, "streams@0x38");
static_assert(offsetof(iw8_focus::GfxImage, fallback) == kGI_fallback, "fallback@0xD8");
static_assert(offsetof(iw8_focus::GfxImage, pixels) == kGI_pixels, "pixels@0xE0");

static inline void s64(uint8_t* p, size_t off, uint64_t v) {
    std::memcpy(p + off, &v, 8);
}
static inline void s32(uint8_t* p, size_t off, uint32_t v) {
    std::memcpy(p + off, &v, 4);
}
static inline void s16(uint8_t* p, size_t off, uint16_t v) {
    std::memcpy(p + off, &v, 2);
}
static inline void s8(uint8_t* p, size_t off, uint8_t v) {
    p[off] = v;
}

static constexpr uint8_t IW8_IMG_CATEGORY_LOAD_FROM_FILE = 3;
static constexpr uint8_t IW8_TS_COLOR_MAP = 2;

void iw8_write_image(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name) {
    if (!name) {
        zt::err("iw8_write_image: null name");
        return;
    }

    // 1) Load the self-describing image blob produced by Stage A.
    std::vector<uint8_t> blob;
    if (!zs.getImage(name, blob) || blob.size() < sizeof(cvtimg::ImageBlobHeader)) {
        zt::err("iw8_write_image: missing/short image blob for '%s'", name);
        return;
    }
    cvtimg::ImageBlobHeader h{};
    std::memcpy(&h, blob.data(), sizeof(h));
    if (std::memcmp(h.magic, cvtimg::kImageBlobMagic, sizeof(h.magic)) != 0 ||
        h.version != cvtimg::kImageBlobVersion) {
        zt::err("iw8_write_image: bad blob magic/version for '%s'", name);
        return;
    }

    // Split out the embedded original name + the pixel mip chain.
    size_t off = sizeof(h);
    std::string realName;
    if (h.nameLen > 0 && off + h.nameLen <= blob.size()) {
        const char* np = reinterpret_cast<const char*>(blob.data() + off);
        realName.assign(np); // up to NUL within nameLen
        off += h.nameLen;
    } else {
        realName = name; // fall back to the manifest/stem name
    }
    const uint8_t* pix = blob.data() + off;
    uint32_t pixAvail = (off <= blob.size()) ? static_cast<uint32_t>(blob.size() - off) : 0u;
    // Trust/repair the pixel size against format+dims (convert/image.cpp).
    uint32_t pixSize = cvtimg::validate_pixel_size(h, pixAvail);

    // 2) Convert IW3 fields -> IW8 GfxImage and stamp the 0xE8 struct.
    const uint32_t iw8Format = cvtimg::iw3_to_iw8_pixel_format(h.iw3Format);
    const uint8_t semantic = h.semantic ? h.semantic : IW8_TS_COLOR_MAP;
    const uint8_t category = h.category ? h.category : IW8_IMG_CATEGORY_LOAD_FROM_FILE;
    const uint8_t levelCount = h.levelCount ? h.levelCount : 1;
    const uint32_t totalSize =
        pixSize ? pixSize : cvtimg::iw3_total_size(h.iw3Format, h.width, h.height, levelCount);

    uint8_t gi[kImageSize];
    std::memset(gi, 0, sizeof(gi));
    s64(gi, kGI_name, iw8::PTR_FOLLOWS);     // name follows inline (-2)
    s64(gi, kGI_packedAtlas, iw8::PTR_NULL); // no atlas blob
    s32(gi, kGI_textureId, 0);               // runtime; 0
    s32(gi, kGI_format, iw8Format);
    s32(gi, kGI_flags, 0); // GfxImageFlags (none for resident)
    s32(gi, kGI_totalSize, totalSize);
    s32(gi, kGI_semanticSpec, 0);
    s16(gi, kGI_width, h.width);
    s16(gi, kGI_height, h.height);
    s16(gi, kGI_depth, h.depth ? h.depth : 1);
    s16(gi, kGI_numElements, h.numElements ? h.numElements : 1);
    s16(gi, kGI_atlasInfo, 0);
    s8(gi, kGI_semantic, semantic);
    s8(gi, kGI_category, category);
    s8(gi, kGI_levelCount, levelCount);
    s8(gi, kGI_streamedPart, 0); // 0 => fully resident (no xpak)
    s8(gi, kGI_decalAtlasIdx, 0);
    s8(gi, kGI_freqBias, 0);
    // streams[4] @0x38 (0xA0): left zeroed (no streamed parts).
    s64(gi, kGI_fallback, iw8::PTR_NULL);

    s64(gi, kGI_pixels, pixSize ? iw8::PTR_FOLLOWS : iw8::PTR_NULL);

    // 3) Emit in load-read order. struct -> TEMP_PRELOAD(1); name + pixels -> VIRTUAL(8).
    zw.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7); // 8-align the struct
    zw.write(gi, sizeof(gi));
    zw.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
    zw.writeStr(realName); // GfxImage.name (the TRUE asset name)
    if (pixSize) {
        zw.align(15);           // 16-align pixel data (DXT block alignment)
        zw.write(pix, pixSize); // the raw DXT/RGBA mip chain (resident inline)
    }
    zw.popStream();
    zw.popStream();

    zt::info("iw8_write_image: '%s' %ux%u fmt=%u(iw3=%d) L=%u %s%u px -> GfxImage(0xE8)",
             realName.c_str(), (unsigned)h.width, (unsigned)h.height, iw8Format, h.iw3Format,
             (unsigned)levelCount, pixSize ? "" : "(no) ", pixSize);
}

} // namespace convert
