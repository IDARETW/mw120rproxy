#include "common/fs_util.h"
#include "common/log.h"
#include "convert/image_fmt.h"
#include "image_dump.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace dumpimg
{

using namespace zt;

// ---- little-endian cursor over the in-memory .ffImg bytes (FileReader mirror, bounds-checked)
// ----
namespace
{
struct Cur
{
    const uint8_t *p;
    size_t n;
    size_t at = 0;
    bool ok = true;

    bool need(size_t k)
    {
        if (!ok)
            return false;
        if (at + k > n)
        {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t rd_u8()
    {
        if (!need(1))
            return 0;
        return p[at++];
    }
    int8_t rd_i8()
    {
        return (int8_t)rd_u8();
    }
    int32_t rd_i32()
    {
        if (!need(4))
            return 0;
        int32_t v;
        std::memcpy(&v, p + at, 4);
        at += 4;
        return v;
    }
    // NUL-terminated C-string (FileReader ReadString). Empty + ok=false if unterminated.
    std::string rd_cstr()
    {
        std::string s;
        while (ok && at < n)
        {
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
std::string cleanImageName(const std::string &name)
{
    std::string out = name;
    for (char &c : out)
        if (c == '*')
            c = '_';
    return out;
}

std::vector<std::string> listImageDumps(const std::string &dumpDir)
{
    std::vector<std::string> out, files;
    const std::string imagesDir = path_join(dumpDir, "images");
    if (!list_dir(imagesDir, files))
        return out;
    for (const auto &f : files)
    {
        std::string extension = std::filesystem::path(f).extension().string();
        for (char &c : extension)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (extension == ".ffimg" || extension == ".iwi")
        {
            out.push_back(std::filesystem::path(f).stem().string());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

ImageDumpFile readImageDump(const std::string &dumpDir, const std::string &nameOrStem)
{
    ImageDumpFile d;

    // Resolve the file: the on-disk name uses the CLEANED stem (no '*'). Try the cleaned form of
    // the given name first; if the caller already passed a stem, cleaning is a no-op so it still
    // resolves.
    const std::string stem = cleanImageName(nameOrStem);
    d.fileStem = stem;
    const std::string imagesDir = path_join(dumpDir, "images");
    std::string path = path_join(imagesDir, stem + ".ffImg");
    std::vector<uint8_t> buf;
    if (!read_file(path, buf))
    {
        path = path_join(imagesDir, stem + ".iwi");
    }
    if (buf.empty() && !read_file(path, buf))
    {
        // Case-insensitive fallback for both supported dump extensions.
        bool found = false;
        std::vector<std::string> files;
        if (list_dir(imagesDir, files))
        {
            for (const auto &f : files)
            {
                const std::filesystem::path candidate(f);
                std::string extension = candidate.extension().string();
                for (char &c : extension)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (candidate.stem().string() == stem &&
                    (extension == ".ffimg" || extension == ".iwi"))
                {
                    path = path_join(imagesDir, f);
                    if (read_file(path, buf))
                    {
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found)
        {
            warn("dumpimg: no .ffImg or .iwi for '%s' under %s", nameOrStem.c_str(),
                 imagesDir.c_str());
            return d;
        }
    }

    std::string extension = std::filesystem::path(path).extension().string();
    for (char &character : extension)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (extension == ".iwi")
    {
        constexpr size_t headerSize = 28;
        if (buf.size() < headerSize || std::memcmp(buf.data(), "IWi", 3) != 0 || buf[3] != 6)
        {
            warn("dumpimg: '%s' is not a supported IW3 IWI v6 image", path.c_str());
            return d;
        }

        const int32_t format = buf[4];
        const uint16_t width = uint16_t(buf[6] | (uint16_t(buf[7]) << 8));
        const uint16_t height = uint16_t(buf[8] | (uint16_t(buf[9]) << 8));
        const uint16_t depth = uint16_t(buf[10] | (uint16_t(buf[11]) << 8));
        const size_t levelSize = cvtimg::levelSize(format, width, height);
        if (!cvtimg::isBlockCompressed(format) || !width || !height || depth != 1 ||
            width > 4096 || height > 4096 || levelSize != buf.size() - headerSize)
        {
            warn("dumpimg: '%s' needs a single resident DXT1, DXT3, or DXT5 surface", path.c_str());
            return d;
        }

        d.mapType = IW5_MAPTYPE_2D;
        d.flags = buf[5];
        d.width = width;
        d.height = height;
        d.depth = depth;
        d.mipLevels = 1;
        d.dimensions[0] = width;
        d.dimensions[1] = height;
        d.dimensions[2] = depth;
        d.format = format == cvtimg::IWI_DXT1   ? 0x31545844
                   : format == cvtimg::IWI_DXT3 ? 0x33545844
                                                : 0x35545844;
        d.dataSize = static_cast<int32_t>(levelSize);
        d.name = stem;
        d.pixels.assign(buf.begin() + headerSize, buf.end());
        d.loaded = true;
        info("dumpimg: read IW3 IWI '%s' %ux%u fmt=%d resident px=%zu", d.name.c_str(), width,
             height, format, d.pixels.size());
        return d;
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

    if (!c.ok || d.name.empty())
    {
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

    if (!c.ok)
    {
        warn("dumpimg: '%s' loaddef parse failed (@%zu/%zu)", d.name.c_str(), c.at, buf.size());
        return d;
    }

    // pixels: dataSize bytes, clamped to what is actually present (defensive — file is untrusted).
    uint32_t want = (d.dataSize > 0) ? (uint32_t)d.dataSize : 0u;
    uint32_t avail = (c.at <= buf.size()) ? (uint32_t)(buf.size() - c.at) : 0u;
    uint32_t take = want < avail ? want : avail;
    if (take)
        d.pixels.assign(buf.begin() + c.at, buf.begin() + c.at + take);
    if (want != avail)
    {
        // not fatal: the dump's dataSize is frequently stale (see below); we keep `take` bytes.
        debug("dumpimg: '%s' dataSize=%d but %u bytes remain after header (kept %u)",
              d.name.c_str(), d.dataSize, avail, take);
    }

    // STALENESS heuristic for the LoadDef block. For mp_test's map sidecars the LoadDef is stale
    // heap: dataSize is tiny (3/21) yet height*width imply a large surface, and dimensions[] don't
    // match width/height. Flag it so the converter trusts the HEADER dims, not the LoadDef. Pure
    // heuristic; it never blocks the read (the image still emits).
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
