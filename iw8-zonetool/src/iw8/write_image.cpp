

#include "../dumpsrc/image_dump.h"
#include "iw8_zone.h"
#include "iw8_zonebuffer.h"
#include "iw8_structs.h"
#include "iw8_focus_structs.h"
#include "../common/log.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dumpimg {

using namespace zt;

// GfxImage(19) = 0xE8 (g_assetSizes). Field offsets — bound to the pinned struct below.
static constexpr size_t kImageSize = iw8sz::IMAGE; // 0xE8
static constexpr size_t kGI_name = 0x00;           // const char*  //PTR
static constexpr size_t kGI_packedAtlas = 0x08;    // uint8_t*     //PTR
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

// keep the literal offsets locked to the pinned struct (compile-time guard against drift).
static_assert(sizeof(iw8_focus::GfxImage) == kImageSize, "GfxImage must be 0xE8");
static_assert(offsetof(iw8_focus::GfxImage, format) == kGI_format, "format@0x14");
static_assert(offsetof(iw8_focus::GfxImage, flags) == kGI_flags, "flags@0x18");
static_assert(offsetof(iw8_focus::GfxImage, totalSize) == kGI_totalSize, "totalSize@0x1C");
static_assert(offsetof(iw8_focus::GfxImage, width) == kGI_width, "width@0x24");
static_assert(offsetof(iw8_focus::GfxImage, height) == kGI_height, "height@0x26");
static_assert(offsetof(iw8_focus::GfxImage, depth) == kGI_depth, "depth@0x28");
static_assert(offsetof(iw8_focus::GfxImage, numElements) == kGI_numElements, "numElements@0x2A");
static_assert(offsetof(iw8_focus::GfxImage, semantic) == kGI_semantic, "semantic@0x2E");
static_assert(offsetof(iw8_focus::GfxImage, category) == kGI_category, "category@0x2F");
static_assert(offsetof(iw8_focus::GfxImage, levelCount) == kGI_levelCount, "levelCount@0x30");
static_assert(offsetof(iw8_focus::GfxImage, streamedPartCount) == kGI_streamedPart,
              "streamedPart@0x31");
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

bool emitImageBody(iw8::ZoneWriter& zw, const Iw8ImageDef& def) {
    if (def.name.empty()) {
        err("dumpimg: emitImageBody: empty image name");
        return false;
    }

    const uint32_t pixSize = (uint32_t)def.pixels.size();

    // 1) build the fixed 0xE8 GfxImage struct.
    uint8_t gi[kImageSize];
    std::memset(gi, 0, sizeof(gi));
    s64(gi, kGI_name, iw8::PTR_FOLLOWS);     // name follows inline (-2)
    s64(gi, kGI_packedAtlas, iw8::PTR_NULL); // no atlas blob
    s32(gi, kGI_textureId, 0);               // runtime; 0
    s32(gi, kGI_format, def.format);
    s32(gi, kGI_flags, def.flags); // GfxImageFlags
    s32(gi, kGI_totalSize, def.totalSize);
    s32(gi, kGI_semanticSpec, 0);
    s16(gi, kGI_width, def.width);
    s16(gi, kGI_height, def.height);
    s16(gi, kGI_depth, def.depth ? def.depth : 1);
    s16(gi, kGI_numElements, def.numElements ? def.numElements : 1);
    s16(gi, kGI_atlasInfo, 0);
    s8(gi, kGI_semantic, def.semantic);
    s8(gi, kGI_category, def.category);
    s8(gi, kGI_levelCount, def.levelCount ? def.levelCount : 1);
    s8(gi, kGI_streamedPart, 0); // 0 => fully resident (no xpak)
    s8(gi, kGI_decalAtlasIdx, 0);
    s8(gi, kGI_freqBias, 0);
    // streams[4] @0x38 (0xA0): left zeroed (no streamed parts).
    s64(gi, kGI_fallback, iw8::PTR_NULL); // no low-res fallback
    s64(gi, kGI_pixels, pixSize ? iw8::PTR_FOLLOWS : iw8::PTR_NULL);

    zw.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(15); // 16-align the struct (Load_GfxImagePtr FixStreamAlignment(0xF))
    zw.write(gi, sizeof(gi));
    zw.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
    zw.writeStr(def.name); // GfxImage.name (the TRUE asset name)
    zw.popStream();
    if (pixSize) {
        zw.pushStream(iw8::XFILE_BLOCK_UNK7); // resident pixels live in stream 7
        zw.align(0xFF); // 256-align (Load_GfxImagePixels FixStreamAlignment(0xFF))
        zw.write(def.pixels.data(), pixSize); // resident inline pixel payload
        zw.popStream();
    }
    zw.popStream();

    info("dumpimg: emit GfxImage '%s' %ux%u fmt=%u sem=%u cat=%u L=%u %s%u px -> 0xE8",
         def.name.c_str(), (unsigned)def.width, (unsigned)def.height, def.format, def.semantic,
         def.category, def.levelCount, pixSize ? "" : "(no) ", pixSize);
    return true;
}

bool addImageAsset(iw8::ZoneWriter& zw, const Iw8ImageDef& def) {
    if (def.name.empty()) {
        err("dumpimg: addImageAsset: empty image name");
        return false;
    }
    Iw8ImageDef copy = def; // capture by value so the deferred callback owns its data
    zw.add(ASSET_TYPE_IMAGE, def.name, [copy](iw8::ZoneWriter& w) {
        emitImageBody(w, copy);
    });
    return true;
}

size_t buildImageZone(iw8::ZoneBuffer& zb, const std::vector<Iw8ImageDef>& defs) {
    using namespace iw8;

    // 1) XAssetList root -> TEMP(0).
    zb.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = 0;
    al.stringList.loaded = 0;
    al.stringList.strings = PTR_NULL;
    al.assetCount = (uint32_t)defs.size();
    al.assetReadPos = 0;
    al.assets = PTR_FOLLOWS;
    zb.writeT(al);
    zb.popStream();

    // 2) XAsset[N] -> VIRTUAL(8): each image(19), header=-3 (insert).
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.align(7);
    for (size_t i = 0; i < defs.size(); ++i) {
        IW8_XAsset a{};
        a.type = ASSET_TYPE_IMAGE;
        a.header = PTR_INSERT;
        zb.writeT(a);
    }

    // 3) each image body, in order, directly on the ZoneBuffer (mirror emitImageBody, no ZoneWriter).
    for (const auto& def : defs) {
        const uint32_t pixSize = (uint32_t)def.pixels.size();
        uint8_t gi[kImageSize];
        std::memset(gi, 0, sizeof(gi));
        s64(gi, kGI_name, PTR_FOLLOWS);
        s64(gi, kGI_packedAtlas, PTR_NULL);
        s32(gi, kGI_format, def.format);
        s32(gi, kGI_flags, def.flags);
        s32(gi, kGI_totalSize, def.totalSize);
        s16(gi, kGI_width, def.width);
        s16(gi, kGI_height, def.height);
        s16(gi, kGI_depth, def.depth ? def.depth : 1);
        s16(gi, kGI_numElements, def.numElements ? def.numElements : 1);
        s8(gi, kGI_semantic, def.semantic);
        s8(gi, kGI_category, def.category);
        s8(gi, kGI_levelCount, def.levelCount ? def.levelCount : 1);
        s8(gi, kGI_streamedPart, 0);
        s64(gi, kGI_fallback, PTR_NULL);
        s64(gi, kGI_pixels, pixSize ? PTR_FOLLOWS : PTR_NULL);

        zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
        zb.align(15); // struct 16-align
        zb.write(gi, sizeof(gi));
        zb.pushStream(XFILE_BLOCK_VIRTUAL);
        zb.writeStr(def.name.c_str()); // name -> VIRTUAL(8)
        zb.popStream();
        if (pixSize) {
            zb.pushStream(XFILE_BLOCK_UNK7);
            zb.align(0xFF);
            zb.write(def.pixels.data(), pixSize);
            zb.popStream();
        }
        zb.popStream();
    }

    zb.popStream(); // VIRTUAL (XAsset array) -> TEMP
    return defs.size();
}

} // namespace dumpimg
