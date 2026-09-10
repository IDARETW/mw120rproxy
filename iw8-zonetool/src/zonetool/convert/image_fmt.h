#pragma once

#include <cstdint>

namespace cvtimg
{
enum IwiFormat : int32_t
{
    IWI_INVALID = 0,
    IWI_ARGB32 = 1,
    IWI_RGB24 = 2,
    IWI_GA16 = 3,
    IWI_A8 = 4,
    IWI_DXT1 = 11,
    IWI_DXT3 = 12,
    IWI_DXT5 = 13,
};

enum Iw8PixelFormat : uint32_t
{
    IW8_FMT_R8G8B8A8_UNORM = 6,
    IW8_FMT_BC1_UNORM = 33,
    IW8_FMT_BC2_UNORM = 35,
    IW8_FMT_BC3_UNORM = 37,
    IW8_FMT_BC7_UNORM = 44,
};

inline bool isBlockCompressed(const int32_t format)
{
    return format == IWI_DXT1 || format == IWI_DXT3 || format == IWI_DXT5;
}

inline uint32_t blockBytes(const int32_t format)
{
    switch (format)
    {
    case IWI_DXT1:
        return 8;
    case IWI_DXT3:
    case IWI_DXT5:
        return 16;
    default:
        return 0;
    }
}

inline uint32_t bytesPerPixel(const int32_t format)
{
    switch (format)
    {
    case IWI_ARGB32:
        return 4;
    case IWI_RGB24:
        return 3;
    case IWI_GA16:
        return 2;
    case IWI_A8:
        return 1;
    default:
        return 0;
    }
}

inline uint32_t levelSize(const int32_t format, uint32_t width, uint32_t height)
{
    width = width == 0 ? 1 : width;
    height = height == 0 ? 1 : height;
    if (isBlockCompressed(format))
    {
        return ((width + 3) / 4) * ((height + 3) / 4) * blockBytes(format);
    }
    const uint32_t pixelSize = bytesPerPixel(format);
    return width * height * (pixelSize == 0 ? 4 : pixelSize);
}

inline uint32_t iw3_total_size(const int32_t format, const uint32_t width, const uint32_t height,
                               uint32_t levelCount)
{
    levelCount = levelCount == 0 ? 1 : levelCount;
    uint32_t total = 0;
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        total += levelSize(format, width >> level, height >> level);
    }
    return total;
}
} // namespace cvtimg
