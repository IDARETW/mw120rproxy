// asset_image.cpp — Stage-A IW3 GfxImage reader (SPEC §2b/§3a). Replaces the iw3_assets_stub.cpp
// no-op for the image family. Implements convert::iw3_dump_image = the offline inverse of the IW3
// fastfile load for a GfxImage: read the GfxImage struct inline at the load cursor, walk its two
// pointer fields (texture=GfxImageLoadDef-with-inline-pixels, name), extract the raw DXT/RGBA mip
// chain, and dump a self-describing image blob to images/<name>.iwi via the ZoneSource contract.
//
// LOAD-READ-ORDER (matches zonetool-develop IW3/IW4 IGfxImage + the IW3 DB_LoadXFile convention):
//   GfxImage is serialized as: [GfxImage struct][ (if texture follows) GfxImageLoadDef header +
//   resourceSize pixel bytes ][ (name follows) NUL-terminated name string ]. Pointer fields hold the
//   tagged value 0 (null) / -1 (0xFFFFFFFF follows) / packed-offset (alias). We handle null + follows
//   inline; an aliased texture/name (packed offset) is resolved to its absolute zone offset and read
//   there WITHOUT advancing the main cursor (it lives in already-loaded data).
//
// The cursor on entry points at the GfxImage struct (the loader positioned it via Load_XAsset ->
// Load_GfxImagePtr). On a clean read the cursor ends just past the last inline-following field, ready
// for the next asset — so a future faithful asset-list walk can chain readers.
#include "../convert/registry.h"
#include "../convert/image_fmt.h"
#include "../common/log.h"
#include "iw3_zone.h"
#include "iw3_structs.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <string>
#include <vector>

namespace convert {

// Read a NUL-terminated string starting at absolute zone offset `off` (no cursor move).
static std::string readCStrAt(const iw3::LoadCtx& lc, size_t off) {
    std::string s;
    const uint8_t* base = lc.base();
    size_t n = lc.size();
    for (size_t i = off; i < n; ++i) {
        char c = static_cast<char>(base[i]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

// Read a NUL-terminated string inline at the current cursor, advancing the cursor past the NUL.
static bool readCStrInline(iw3::LoadCtx& lc, std::string& out) {
    out.clear();
    char c = 0;
    for (;;) {
        if (!lc.read(c)) return false;
        if (c == '\0') break;
        out.push_back(c);
    }
    return true;
}

// Sanitize an asset name into a filesystem-safe stem (IW3 map images use '*' / leading specials).
static std::string cleanName(const std::string& name) {
    std::string out = name;
    for (char& ch : out) {
        switch (ch) {
            case '*': ch = '_'; break;
            case '/': case '\\': ch = '_'; break;
            case ':': case '?': case '"': case '<': case '>': case '|': ch = '_'; break;
            default: break;
        }
    }
    if (out.empty()) out = "unnamed_image";
    return out;
}

void iw3_dump_image(iw3::LoadCtx& lc, iw3sr::ZoneSource& zs) {
    using namespace iw3;

    const size_t structPos = lc.pos();

    // 1) Read the GfxImage struct inline (32-bit, pack(4)).
    GfxImage img{};
    if (!lc.read(img)) { zt::err("iw3_dump_image: short read of GfxImage @%zu", structPos); return; }

    // 2) Resolve the name pointer. IW3 serializes `name` as a "follows" string after the texture blob
    //    (load order), or as a packed offset into already-loaded data. We must know the name BEFORE we
    //    know where to write — but the bytes physically follow the texture, so we resolve texture first
    //    and read name after (the loader's order). We therefore defer the name read.

    // 3) Resolve + read the texture (GfxImageLoadDef + inline pixels).
    GfxImageLoadDef ld{};
    std::vector<uint8_t> pixels;
    int32_t  iwiFormat = cvtimg::IWI_INVALID;
    uint8_t  levelCount = 1;
    bool     haveTexture = false;

    {
        bool follows = false;
        size_t texOff = lc.resolve(img.texture.off, &follows);
        if (texOff == LoadCtx::npos) {
            // null texture (delay-loaded / map image with no inline pixels) — dump a metadata-only blob.
            zt::debug("iw3_dump_image: null texture @%zu (no inline pixels)", structPos);
        } else if (follows) {
            // GfxImageLoadDef header is { char levelCount; char flags; i16 dims[3]; int format;
            //   int resourceSize; char data[1] }. The fixed header up to `data` is 16 bytes (pack4):
            //   1+1 + 6 + 4 + 4 = 16, then resourceSize pixel bytes follow inline.
            // Read the fixed header fields explicitly (the struct's trailing data[1] is the inline blob).
            uint8_t lcByte = 0, flByte = 0;
            int16_t dims[3] = {0,0,0};
            int32_t fmt = 0, resSize = 0;
            bool ok = lc.read(lcByte) && lc.read(flByte) &&
                      lc.read(&dims, sizeof(dims)) && lc.read(fmt) && lc.read(resSize);
            if (!ok) { zt::err("iw3_dump_image: short read of GfxImageLoadDef"); return; }
            (void)ld;
            levelCount = lcByte ? lcByte : 1;
            iwiFormat  = fmt;
            haveTexture = true;

            if (resSize < 0 || static_cast<size_t>(resSize) > lc.size()) {
                zt::err("iw3_dump_image: bad resourceSize %d", resSize); return;
            }
            pixels.resize(static_cast<size_t>(resSize));
            if (resSize > 0 && !lc.read(pixels.data(), pixels.size())) {
                zt::err("iw3_dump_image: short read of %d pixel bytes", resSize); return;
            }
            zt::debug("iw3_dump_image: loadDef fmt=%d %dx%dx%d L=%u res=%d",
                      fmt, dims[0], dims[1], dims[2], (unsigned)levelCount, resSize);
        } else {
            // Aliased texture (packed offset) — read the loadDef header from already-loaded data.
            const uint8_t* base = lc.base();
            if (texOff + 16 <= lc.size()) {
                uint8_t lcByte = base[texOff + 0];
                int32_t fmt = 0, resSize = 0;
                std::memcpy(&fmt,     base + texOff + 8,  4);
                std::memcpy(&resSize, base + texOff + 12, 4);
                levelCount = lcByte ? lcByte : 1;
                iwiFormat  = fmt;
                haveTexture = true;
                if (resSize > 0 && texOff + 16 + static_cast<size_t>(resSize) <= lc.size()) {
                    pixels.assign(base + texOff + 16, base + texOff + 16 + resSize);
                }
            }
        }
    }

    // 4) Resolve + read the name (follows after the texture in load order, else a packed offset).
    std::string name;
    {
        bool follows = false;
        size_t nameOff = lc.resolve(img.name.off, &follows);
        if (nameOff == LoadCtx::npos) {
            zt::warn("iw3_dump_image: image @%zu has null name — skipping", structPos);
            return;
        } else if (follows) {
            if (!readCStrInline(lc, name)) { zt::err("iw3_dump_image: short read of name"); return; }
        } else {
            name = readCStrAt(lc, nameOff);
        }
    }
    if (name.empty()) { zt::warn("iw3_dump_image: empty image name @%zu — skipping", structPos); return; }

    // 5) Build the self-describing image blob (header + raw pixel mip chain) and dump it.
    cvtimg::ImageBlobHeader h{};
    std::memcpy(h.magic, cvtimg::kImageBlobMagic, sizeof(h.magic));
    h.version     = cvtimg::kImageBlobVersion;
    h.iw3Format   = haveTexture ? iwiFormat : cvtimg::IWI_INVALID;
    h.mapType     = static_cast<uint8_t>(img.mapType);
    h.semantic    = static_cast<uint8_t>(img.semantic);
    h.category    = static_cast<uint8_t>(img.category);
    h.levelCount  = levelCount;
    h.width       = img.width;
    h.height      = img.height;
    h.depth       = img.depth ? img.depth : 1;
    h.numElements = 1;                              // IW3 has no array textures; cube => 6 faces in pixels
    h.pixelSize   = static_cast<uint32_t>(pixels.size());
    h.flags       = 0;                             // IW3 GfxImage carries no on-struct flags we map here
    h.nameLen     = static_cast<uint16_t>(name.size() + 1);   // original name incl. NUL

    std::vector<uint8_t> blob;
    blob.reserve(sizeof(h) + h.nameLen + pixels.size());
    const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
    blob.insert(blob.end(), hp, hp + sizeof(h));
    blob.insert(blob.end(), name.begin(), name.end());
    blob.push_back(0);                              // NUL-terminate the original name
    blob.insert(blob.end(), pixels.begin(), pixels.end());

    std::string stem = cleanName(name);
    if (!zs.addImage(stem, blob)) {
        zt::err("iw3_dump_image: failed to write images/%s.iwi", stem.c_str());
        return;
    }
    zt::info("iw3_dump_image: dumped '%s' (%ux%u fmt=%d L=%u, %zu px bytes) -> images/%s.iwi",
             name.c_str(), (unsigned)img.width, (unsigned)img.height, iwiFormat,
             (unsigned)levelCount, pixels.size(), stem.c_str());
}

} // namespace convert
