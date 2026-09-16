#include "../../common/log.h"
#include "../dumpsrc/image_dump.h"
#include "image_fmt.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dumpimg
{

using namespace zt;

namespace
{

// IW8 default enum values (IW-line; same as the legacy IW3-path defaults in asset_image.cpp).
constexpr uint8_t IW8_IMG_CATEGORY_LOAD_FROM_FILE = 3; // GfxImageCategory
constexpr uint8_t IW8_TS_COLOR_MAP = 2;                // TextureSemantic (2d color)

// DXT FourCC magics found in an IW5 GfxImageLoadDef.format ("DXT1"/"DXT3"/"DXT5").
constexpr int32_t FOURCC_DXT1 = 0x31545844; // 'DXT1'
constexpr int32_t FOURCC_DXT3 = 0x33545844; // 'DXT3'
constexpr int32_t FOURCC_DXT5 = 0x35545844; // 'DXT5'
constexpr int32_t DXGI_BC1_UNORM = 71;
constexpr int32_t DXGI_BC1_SRGB = 72;
constexpr int32_t DXGI_BC2_UNORM = 74;
constexpr int32_t DXGI_BC2_SRGB = 75;
constexpr int32_t DXGI_BC3_UNORM = 77;
constexpr int32_t DXGI_BC3_SRGB = 78;

constexpr uint32_t IW8_FMT_BC1_SRGB = 34;
constexpr uint32_t IW8_FMT_BC2_SRGB = 36;
constexpr uint32_t IW8_FMT_BC3_SRGB = 38;

// Map the IW5 LoadDef format to Replay's GfxPixelFormat values.
bool mapIw5Format(int32_t iw5fmt, uint32_t &iw8fmt, bool &isBlock, uint32_t &unitBytes)
{
    isBlock = false;
    unitBytes = 0;
    switch (iw5fmt)
    {
    case FOURCC_DXT1:
    case DXGI_BC1_UNORM:
        iw8fmt = cvtimg::IW8_FMT_BC1_UNORM;
        isBlock = true;
        unitBytes = 8;
        return true;
    case DXGI_BC1_SRGB:
        iw8fmt = IW8_FMT_BC1_SRGB;
        isBlock = true;
        unitBytes = 8;
        return true;
    case FOURCC_DXT3:
    case DXGI_BC2_UNORM:
        iw8fmt = cvtimg::IW8_FMT_BC2_UNORM;
        isBlock = true;
        unitBytes = 16;
        return true;
    case DXGI_BC2_SRGB:
        iw8fmt = IW8_FMT_BC2_SRGB;
        isBlock = true;
        unitBytes = 16;
        return true;
    case FOURCC_DXT5:
    case DXGI_BC3_UNORM:
        iw8fmt = cvtimg::IW8_FMT_BC3_UNORM;
        isBlock = true;
        unitBytes = 16;
        return true;
    case DXGI_BC3_SRGB:
        iw8fmt = IW8_FMT_BC3_SRGB;
        isBlock = true;
        unitBytes = 16;
        return true;
    // small numeric codes seen in dumps that clearly denote an uncompressed surface
    case 20:
    case 21: // RGB / ARGB-ish (D3DFMT_R8G8B8 / A8R8G8B8 family)
        iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM;
        unitBytes = 4;
        return true;
    default:
        return false; // unrecognised / stale
    }
}

// Bytes for a single uncompressed RGBA8 mip level.
uint32_t rgba8LevelBytes(uint32_t w, uint32_t h)
{
    if (!w)
        w = 1;
    if (!h)
        h = 1;
    return w * h * 4u;
}

bool round16(const size_t bytes, size_t &rounded)
{
    if (bytes > std::numeric_limits<size_t>::max() - 15)
        return false;
    rounded = (bytes + 15) & ~size_t{15};
    return true;
}

bool sourceSubresourceBytes(const bool isBlock, const uint32_t unitBytes, const uint32_t width,
                            const uint32_t height, const uint32_t depth, size_t &bytes)
{
    const uint64_t w = std::max(1u, width);
    const uint64_t h = std::max(1u, height);
    const uint64_t d = std::max(1u, depth);
    const uint64_t rows = isBlock ? (h + 3) / 4 : h;
    const uint64_t columns = isBlock ? (w + 3) / 4 : w;
    const uint64_t value = columns * rows * d * unitBytes;
    if (value > std::numeric_limits<size_t>::max())
        return false;
    bytes = static_cast<size_t>(value);
    return true;
}

uint32_t residentSliceCount(const ImageDumpFile &in)
{
    const uint32_t elements = in.numElements ? in.numElements : 1u;
    if (in.mapType == IW5_MAPTYPE_CUBE)
        return elements * 6u;
    if (in.mapType == IW5_MAPTYPE_2D)
        return elements;
    return 1;
}

bool residentPayloadSize(const ImageDumpFile &in, const bool isBlock, const uint32_t unitBytes,
                         size_t &total)
{
    total = 0;
    const uint32_t slices = residentSliceCount(in);
    const uint32_t levels = in.mipLevels ? in.mipLevels : 1u;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const uint32_t width = std::max(1, in.width >> level);
        const uint32_t height = std::max(1, in.height >> level);
        const uint32_t depth = in.mapType == IW5_MAPTYPE_3D ? std::max(1, in.depth >> level) : 1;
        size_t raw = 0, stride = 0;
        if (!sourceSubresourceBytes(isBlock, unitBytes, width, height, depth, raw) || !round16(raw, stride) ||
            (slices && stride > (std::numeric_limits<size_t>::max() - total) / slices))
            return false;
        total += stride * slices;
    }
    return true;
}

// DDS stores every array/cube slice's complete mip chain before the next slice. Replay's
// Image_LoadPixels path consumes mip-major data, with each slice of each mip rounded to 16 bytes
// (the same layout exercised by atian-cod-tools/scripts/mw19/image_test.cpp). Keep this transform
// here so write_image.cpp only serializes an already-native resident payload.
bool packResidentPixels(const ImageDumpFile &in, const bool isBlock, const uint32_t unitBytes,
                        std::vector<uint8_t> &packed)
{
    if (in.pixels.empty() || !in.mipLevels || (in.mapType == IW5_MAPTYPE_CUBE && !in.ddsPayload))
        return false;

    const uint32_t slices = residentSliceCount(in);
    if (!in.ddsPayload && slices != 1)
        return false; // no source-side array/cube order is recorded for ffImg payloads

    struct Level
    {
        size_t offset = 0;
        size_t bytes = 0;
        size_t stride = 0;
    };
    std::vector<Level> levels;
    levels.reserve(static_cast<size_t>(slices) * in.mipLevels);

    size_t sourceOffset = 0;
    for (uint32_t slice = 0; slice < slices; ++slice)
    {
        for (uint32_t level = 0; level < in.mipLevels; ++level)
        {
            const uint32_t width = std::max(1, in.width >> level);
            const uint32_t height = std::max(1, in.height >> level);
            const uint32_t depth = in.mapType == IW5_MAPTYPE_3D ? std::max(1, in.depth >> level) : 1;
            size_t raw = 0, stride = 0;
            if (!sourceSubresourceBytes(isBlock, unitBytes, width, height, depth, raw) || !round16(raw, stride) ||
                sourceOffset > in.pixels.size() || raw > in.pixels.size() - sourceOffset)
                return false;
            levels.push_back({sourceOffset, raw, stride});
            sourceOffset += raw;
        }
    }
    if (sourceOffset != in.pixels.size())
        return false;

    size_t outputSize = 0;
    if (!residentPayloadSize(in, isBlock, unitBytes, outputSize))
        return false;
    packed.clear();
    packed.reserve(outputSize);
    for (uint32_t level = 0; level < in.mipLevels; ++level)
    {
        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            const Level &source = levels[static_cast<size_t>(slice) * in.mipLevels + level];
            packed.insert(packed.end(), in.pixels.begin() + source.offset,
                          in.pixels.begin() + source.offset + source.bytes);
            packed.resize(packed.size() + source.stride - source.bytes);
        }
    }
    return packed.size() == outputSize;
}

} // namespace

Iw8ImageDef convertImage(const ImageDumpFile &in)
{
    Iw8ImageDef out;
    out.name = in.name;

    // ---- geometry (HEADER fields — reliable even when the LoadDef is stale) ----
    out.width = (uint16_t)(in.width > 0 ? in.width : 0);
    out.height = (uint16_t)(in.height > 0 ? in.height : 0);
    out.depth = (uint16_t)(in.depth > 0 ? in.depth : 1);
    const uint32_t elements = in.numElements ? in.numElements : 1u;
    out.numElements = static_cast<uint16_t>(elements);

    // ---- semantic / category (carried; sane defaults when zero) ----
    out.semantic = in.semantic ? in.semantic : IW8_TS_COLOR_MAP;
    out.category = in.category ? in.category : IW8_IMG_CATEGORY_LOAD_FROM_FILE;

    // ---- level count (LoadDef mipLevels is stale here -> default 1 unless plainly valid) ----
    out.levelCount =
        (in.mipLevels >= 1 && in.mipLevels <= 16 && !in.loadDefStale) ? in.mipLevels : 1;

    // ---- format (the flagged-unresolved field) ----
    uint32_t iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM;
    bool isBlock = false;
    uint32_t unitBytes = 0;
    const bool mapped = !in.loadDefStale && mapIw5Format(in.format, iw8fmt, isBlock, unitBytes);
    if (!mapped)
    {
        // Stale or unrecognised LoadDef: use a dimensions-derived uncompressed format.
        iw8fmt = cvtimg::IW8_FMT_R8G8B8A8_UNORM;
        isBlock = false;
        if (!in.loadDefStale)
            warn("dumpimg: '%s' unrecognised LoadDef format %d -> RGBA8",
                 in.name.c_str(), in.format);
    }
    out.format = iw8fmt;

    // ---- flags (IW8 GfxImageFlags). Resident map sidecar: no streaming flags. Carry nothing
    // risky. ----
    out.flags = in.mapType == IW5_MAPTYPE_CUBE
                    ? 0x8000u | (elements > 1 ? 0x20000u : 0u)
                    : in.mapType == IW5_MAPTYPE_3D ? 0x10000u : (elements > 1 ? 0x20000u : 0u);
    out.streamedPartCount = 0; // fully resident — no xpak dependency we can't satisfy offline

    // ---- pixels (inline resident) + totalSize ----
    out.pixels = in.pixels; // verbatim dump payload (tiny for map sidecars)
    if (mapped && !out.pixels.empty())
    {
        std::vector<uint8_t> packed;
        if (packResidentPixels(in, isBlock, unitBytes, packed))
            out.pixels = std::move(packed);
        else
            warn("dumpimg: '%s' payload layout is not a complete supported resident chain; preserving bytes",
                 in.name.c_str());
    }
    if (!out.pixels.empty())
    {
        if (out.pixels.size() > UINT32_MAX)
        {
            warn("dumpimg: '%s' resident payload exceeds Replay totalSize", in.name.c_str());
            out.pixels.clear();
            out.totalSize = 0;
        }
        else
            out.totalSize = static_cast<uint32_t>(out.pixels.size());
    }
    else
    {
        // no usable payload: declare a dims-derived totalSize so the field is defined (non-zero).
        // (We do NOT fabricate pixels — the writer keeps pixels=null when empty.)
        size_t residentSize = 0;
        if (mapped && residentPayloadSize(in, isBlock, unitBytes, residentSize) && residentSize <= UINT32_MAX)
        {
            out.totalSize = static_cast<uint32_t>(residentSize);
        }
        else if (isBlock)
        {
            out.totalSize = cvtimg::iw3_total_size((in.format == FOURCC_DXT1)   ? cvtimg::IWI_DXT1
                                                   : (in.format == FOURCC_DXT3) ? cvtimg::IWI_DXT3
                                                                                : cvtimg::IWI_DXT5,
                                                   out.width, out.height, out.levelCount);
        }
        else
        {
            out.totalSize = rgba8LevelBytes(out.width, out.height);
        }
    }

    info("dumpimg: convert '%s' -> IW8 %ux%u d=%u elem=%u fmt=%u sem=%u cat=%u L=%u resident "
         "px=%zu totalSize=%u",
         out.name.c_str(), out.width, out.height, out.depth, out.numElements, out.format,
         out.semantic, out.category, out.levelCount, out.pixels.size(), out.totalSize);
    return out;
}

} // namespace dumpimg
