#include "common/fs_util.h"
#include "common/log.h"
#include "convert/image_fmt.h"
#include "image_dump.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>

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
        if (extension == ".dds" || extension == ".ffimg" || extension == ".iwi")
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
        path = path_join(imagesDir, stem + ".dds");
    }
    if (buf.empty() && !read_file(path, buf))
    {
        // Case-insensitive fallback for every supported dump extension.
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
                    (extension == ".dds" || extension == ".ffimg" || extension == ".iwi"))
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
            warn("dumpimg: no .dds, .ffImg, or .iwi for '%s' under %s", nameOrStem.c_str(),
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

    if (extension == ".dds")
    {
        constexpr size_t headerSize = 128;
        constexpr size_t dx10HeaderSize = 20;
        constexpr uint32_t fourCcDxt1 = 0x31545844;
        constexpr uint32_t fourCcDxt3 = 0x33545844;
        constexpr uint32_t fourCcDxt5 = 0x35545844;
        constexpr uint32_t fourCcDx10 = 0x30315844;
        constexpr uint32_t dxgiBc1Unorm = 71;
        constexpr uint32_t dxgiBc1Srgb = 72;
        constexpr uint32_t dxgiBc2Unorm = 74;
        constexpr uint32_t dxgiBc2Srgb = 75;
        constexpr uint32_t dxgiBc3Unorm = 77;
        constexpr uint32_t dxgiBc3Srgb = 78;
        if (buf.size() < headerSize || std::memcmp(buf.data(), "DDS ", 4) != 0)
        {
            warn("dumpimg: '%s' has an invalid DDS header", path.c_str());
            return d;
        }
        auto u32 = [&](const size_t offset) {
            uint32_t value = 0;
            std::memcpy(&value, buf.data() + offset, sizeof(value));
            return value;
        };
        if (u32(4) != 124 || u32(76) != 32)
        {
            warn("dumpimg: '%s' has an unsupported DDS header", path.c_str());
            return d;
        }
        const uint32_t height = u32(12);
        const uint32_t width = u32(16);
        const uint32_t mipLevels = std::max(1u, u32(28));
        const uint32_t format = u32(84);
        const uint32_t caps2 = u32(112);
        const bool dx10 = format == fourCcDx10;
        uint32_t dataOffset = static_cast<uint32_t>(headerSize);
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t sourceSlices = 1;
        uint8_t mapType = IW5_MAPTYPE_2D;
        bool supported = format == fourCcDxt1 || format == fourCcDxt3 || format == fourCcDxt5;
        if (dx10)
        {
            if (buf.size() < headerSize + dx10HeaderSize)
                supported = false;
            else
            {
                const uint32_t dxgi = u32(headerSize);
                const uint32_t resourceDimension = u32(headerSize + 4);
                const uint32_t miscFlag = u32(headerSize + 8);
                arraySize = u32(headerSize + 12);
                const bool cube = (miscFlag & 0x4u) != 0;
                const bool volume = resourceDimension == 4;
                const bool texture2D = resourceDimension == 3;
                supported = (dxgi == dxgiBc1Unorm || dxgi == dxgiBc1Srgb ||
                             dxgi == dxgiBc2Unorm || dxgi == dxgiBc2Srgb ||
                             dxgi == dxgiBc3Unorm || dxgi == dxgiBc3Srgb) &&
                            (texture2D || volume) && arraySize && arraySize <= 2048 &&
                            (!cube || (texture2D && width == height)) &&
                            (!volume || (!cube && arraySize == 1 && u32(24)));
                if (supported)
                {
                    depth = volume ? u32(24) : 1;
                    sourceSlices = cube ? arraySize * 6u : arraySize;
                    mapType = cube ? IW5_MAPTYPE_CUBE : (volume ? IW5_MAPTYPE_3D : IW5_MAPTYPE_2D);
                    dataOffset = static_cast<uint32_t>(headerSize + dx10HeaderSize);
                }
            }
        }
        else
        {
            const bool cube = (caps2 & 0x200u) != 0;
            const bool volume = (caps2 & 0x200000u) != 0;
            supported = supported && !volume && (!cube || (caps2 & 0xFC00u) == 0xFC00u) &&
                        (!cube || width == height);
            if (supported)
            {
                sourceSlices = cube ? 6u : 1u;
                mapType = cube ? IW5_MAPTYPE_CUBE : IW5_MAPTYPE_2D;
            }
        }
        if (!width || !height || width > 8192 || height > 8192 || depth > 8192 || mipLevels > 14 ||
            !supported || dataOffset > buf.size())
        {
            warn("dumpimg: '%s' needs a complete supported BC1/BC2/BC3 DDS", path.c_str());
            return d;
        }
        const uint32_t sourceFormat = dx10 ? u32(headerSize) : format;
        const uint64_t blockBytes = sourceFormat == dxgiBc1Unorm || sourceFormat == dxgiBc1Srgb ||
                                             sourceFormat == fourCcDxt1
                                         ? 8
                                         : 16;
        uint64_t expected = 0;
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const uint64_t levelWidth = std::max(1u, width >> level);
            const uint64_t levelHeight = std::max(1u, height >> level);
            const uint64_t levelDepth = mapType == IW5_MAPTYPE_3D ? std::max(1u, depth >> level) : 1;
            const uint64_t levelBytes = ((levelWidth + 3) / 4) * ((levelHeight + 3) / 4) * levelDepth *
                                        blockBytes;
            if (levelBytes > UINT64_MAX / sourceSlices || expected > UINT64_MAX - levelBytes * sourceSlices)
            {
                expected = UINT64_MAX;
                break;
            }
            expected += levelBytes * sourceSlices;
        }
        if (expected > std::numeric_limits<int32_t>::max() || expected != buf.size() - dataOffset)
        {
            warn("dumpimg: '%s' has an inconsistent DDS mip payload", path.c_str());
            return d;
        }

        d.mapType = mapType;
        d.width = static_cast<int32_t>(width);
        d.height = static_cast<int32_t>(height);
        d.depth = static_cast<int32_t>(depth);
        d.numElements = static_cast<uint16_t>(arraySize);
        d.mipLevels = static_cast<uint8_t>(mipLevels);
        d.dimensions[0] = d.width;
        d.dimensions[1] = d.height;
        d.dimensions[2] = d.depth;
        d.format = static_cast<int32_t>(sourceFormat);
        d.dataSize = static_cast<int32_t>(expected);
        d.name = stem;
        d.ddsPayload = true;
        d.pixels.assign(buf.begin() + dataOffset, buf.end());
        d.loaded = true;
        info("dumpimg: read DDS '%s' %ux%u slices=%u array=%u mips=%u resident px=%zu",
             d.name.c_str(), width, height, sourceSlices, arraySize, mipLevels, d.pixels.size());
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
