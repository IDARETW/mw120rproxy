

#include "image_dump.h"
#include "common/fs_util.h"
#include "common/log.h"
#include <cctype>
#include <cstring>
#include <cstdint>

namespace dumpimg {

using namespace zt;

// ---- little-endian cursor over the in-memory .ffImg bytes (FileReader mirror, bounds-checked) ----
namespace {
struct Cur {
    const uint8_t* p;
    size_t n;
    size_t at = 0;
    bool ok = true;

    bool need(size_t k) {
        if (!ok)
            return false;
        if (at + k > n) {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t rd_u8() {
        if (!need(1))
            return 0;
        return p[at++];
    }
    int8_t rd_i8() {
        return (int8_t)rd_u8();
    }
    int32_t rd_i32() {
        if (!need(4))
            return 0;
        int32_t v;
        std::memcpy(&v, p + at, 4);
        at += 4;
        return v;
    }
    // NUL-terminated C-string (FileReader ReadString). Empty + ok=false if unterminated.
    std::string rd_cstr() {
        std::string s;
        while (ok && at < n) {
            char c = (char)p[at++];
            if (c == '\0')
                return s;
            s.push_back(c);
        }
        ok = false; // ran off the end without a NUL
        return s;
    }
};
} // namespace

// IW5 ClearAssetName: replace '*' with '_' to form the on-disk file stem (GfxImage.cpp:373-389).
// (CoD4 map images are named "*lightmap0_primary" / "$outdoor"; '$' is filesystem-safe and kept.)
std::string cleanImageName(const std::string& name) {
    std::string out = name;
    for (char& c : out)
        if (c == '*')
            c = '_';
    return out;
}

std::vector<std::string> listImageDumps(const std::string& dumpDir) {
    std::vector<std::string> out, files;
    const std::string imagesDir = path_join(dumpDir, "images");
    if (!list_dir(imagesDir, files))
        return out;
    for (const auto& f : files) {
        // accept both ".ffImg" (dump's casing) and any-case extension defensively.
        if (f.size() > 6) {
            std::string ext = f.substr(f.size() - 6);
            for (char& c : ext)
                c = (char)std::tolower((unsigned char)c);
            if (ext == ".ffimg") {
                out.push_back(f.substr(0, f.size() - 6));
                continue;
            }
        }
    }
    return out;
}

ImageDumpFile readImageDump(const std::string& dumpDir, const std::string& nameOrStem) {
    ImageDumpFile d;

    // Resolve the file: the on-disk name uses the CLEANED stem (no '*'). Try the cleaned form of the
    // given name first; if the caller already passed a stem, cleaning is a no-op so it still resolves.
    const std::string stem = cleanImageName(nameOrStem);
    d.fileStem = stem;
    const std::string imagesDir = path_join(dumpDir, "images");
    std::string path = path_join(imagesDir, stem + ".ffImg");
    std::vector<uint8_t> buf;
    if (!read_file(path, buf)) {
        // case-insensitive fallback on the extension only ('.ffimg' etc.)
        bool found = false;
        std::vector<std::string> files;
        if (list_dir(imagesDir, files)) {
            for (const auto& f : files) {
                if (f.size() > 6 && f.compare(0, stem.size(), stem) == 0) {
                    std::string ext = f.substr(f.size() - 6);
                    for (char& c : ext)
                        c = (char)std::tolower((unsigned char)c);
                    if (ext == ".ffimg" && f.size() == stem.size() + 6) {
                        path = path_join(imagesDir, f);
                        if (read_file(path, buf)) {
                            found = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!found) {
            warn("dumpimg: no .ffImg for '%s' at %s", nameOrStem.c_str(), path.c_str());
            return d;
        }
    }

    Cur c{buf.data(), buf.size()};

    // ---- header (4 char + 6 int + name) ----
    d.mapType = c.rd_u8();
    d.semantic = c.rd_u8();
    d.category = c.rd_u8();
    d.flags = c.rd_u8();
    d.cardMemory = c.rd_i32();
    d.dataLen1 = c.rd_i32();
    d.dataLen2 = c.rd_i32();
    d.height = c.rd_i32();
    d.width = c.rd_i32();
    d.depth = c.rd_i32();
    d.name = c.rd_cstr();

    if (!c.ok || d.name.empty()) {
        warn("dumpimg: '%s' header/name parse failed (@%zu/%zu)", nameOrStem.c_str(), c.at,
             buf.size());
        return d;
    }

    // ---- GfxImageLoadDef (2 char + 5 int + dataSize pixels) ----
    d.mipLevels = c.rd_u8();
    d.ldFlags = c.rd_u8();
    d.dimensions[0] = c.rd_i32();
    d.dimensions[1] = c.rd_i32();
    d.dimensions[2] = c.rd_i32();
    d.format = c.rd_i32();
    d.dataSize = c.rd_i32();

    if (!c.ok) {
        warn("dumpimg: '%s' loaddef parse failed (@%zu/%zu)", d.name.c_str(), c.at, buf.size());
        return d;
    }

    // pixels: dataSize bytes, clamped to what is actually present (defensive — file is untrusted).
    uint32_t want = (d.dataSize > 0) ? (uint32_t)d.dataSize : 0u;
    uint32_t avail = (c.at <= buf.size()) ? (uint32_t)(buf.size() - c.at) : 0u;
    uint32_t take = want < avail ? want : avail;
    if (take)
        d.pixels.assign(buf.begin() + c.at, buf.begin() + c.at + take);
    if (want != avail) {

        debug("dumpimg: '%s' dataSize=%d but %u bytes remain after header (kept %u)",
              d.name.c_str(), d.dataSize, avail, take);
    }

    // STALENESS heuristic for the LoadDef block. For mp_test's map sidecars the LoadDef is stale heap:
    // dataSize is tiny (3/21) yet height*width imply a large surface, and dimensions[] don't match
    // width/height. Flag it so the converter trusts the HEADER dims, not the LoadDef. Pure heuristic;
    // it never blocks the read (the image still emits).
    {
        const bool tinyPayload = (d.dataSize >= 0 && d.dataSize <= 64);
        const bool bigSurface = ((int64_t)d.width * d.height >= 4096); // >=64x64
        const bool dimMismatch = (d.dimensions[0] != d.width || d.dimensions[1] != d.height);
        d.loadDefStale = tinyPayload && bigSurface && dimMismatch;
    }

    d.loaded = true;
    info("dumpimg: read '%s' %dx%dx%d mapType=%u sem=%u cat=%u flags=%u ldFmt=%d dataSize=%d%s",
         d.name.c_str(), d.width, d.height, d.depth, d.mapType, d.semantic, d.category, d.flags,
         d.format, d.dataSize, d.loadDefStale ? " (LoadDef STALE -> header dims trusted)" : "");
    return d;
}

} // namespace dumpimg
