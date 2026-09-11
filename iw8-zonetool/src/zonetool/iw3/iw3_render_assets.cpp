#include "iw3_render_assets.h"

#include "resources.h"

#include "common/fs_util.h"

#include <Windows.h>
#include <bcrypt.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace iw3
{
namespace
{
using Json = nlohmann::json;

struct Image
{
    unsigned width{};
    unsigned height{};
    std::vector<std::uint8_t> rgba;
};

struct Cubemap
{
    unsigned width{};
    unsigned height{};
    unsigned mipCount{};
    std::vector<std::vector<Image>> faces;
    std::vector<std::uint8_t> residentPixels;
};

struct SourceMaterial
{
    std::string name;
    std::string color;
    std::string normal;
    std::string response;
    std::array<float, 4> tint{1, 1, 1, 1};
    std::array<float, 4> environment{0.8f, 4.0f, 2.5f, 0.625f};
    SurfaceKind kind{SurfaceKind::opaque};
    unsigned flags{};
};

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path &path)
{
    std::vector<std::uint8_t> data;
    if (!zt::read_file(path.string(), data))
        throw std::runtime_error("cannot read " + path.string());
    return data;
}

void WriteBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &data)
{
    std::filesystem::create_directories(path.parent_path());
    if (!zt::write_file(path.string(), data))
        throw std::runtime_error("cannot write " + path.string());
}

Json ReadJson(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read " + path.string());
    return Json::parse(input);
}

void WriteJson(const std::filesystem::path &path, const Json &value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
    output << value;
    if (!output)
        throw std::runtime_error("cannot finish " + path.string());
}

std::string ResourceText(const unsigned id)
{
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource)
        throw std::runtime_error("iw3 converter resource is missing");
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto *data = static_cast<const char *>(LockResource(loaded));
    const DWORD size = SizeofResource(nullptr, resource);
    if (!loaded || !data || !size)
        throw std::runtime_error("iw3 converter resource is empty");
    return std::string(data, data + size);
}

template <class T> T Read(const std::span<const std::uint8_t> data, const std::size_t offset)
{
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("truncated DDS image");
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

std::uint8_t Expand5(const unsigned value)
{
    return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

std::uint8_t Expand6(const unsigned value)
{
    return static_cast<std::uint8_t>((value << 2) | (value >> 4));
}

std::array<std::uint8_t, 4> Color565(const std::uint16_t value)
{
    return {Expand5((value >> 11) & 31), Expand6((value >> 5) & 63), Expand5(value & 31), 255};
}

void StorePixel(Image &image, const unsigned x, const unsigned y,
                const std::array<std::uint8_t, 4> &color)
{
    if (x >= image.width || y >= image.height)
        return;
    std::memcpy(image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4,
                color.data(), 4);
}

void DecodeColorBlock(Image &image, const unsigned blockX, const unsigned blockY,
                      const std::uint8_t *block, const bool dxt1)
{
    std::array<std::array<std::uint8_t, 4>, 4> colors{};
    const std::uint16_t a = static_cast<std::uint16_t>(block[0] | block[1] << 8);
    const std::uint16_t b = static_cast<std::uint16_t>(block[2] | block[3] << 8);
    colors[0] = Color565(a);
    colors[1] = Color565(b);
    if (!dxt1 || a > b)
    {
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            colors[2][channel] = static_cast<std::uint8_t>((2 * colors[0][channel] + colors[1][channel]) / 3);
            colors[3][channel] = static_cast<std::uint8_t>((colors[0][channel] + 2 * colors[1][channel]) / 3);
        }
        colors[2][3] = colors[3][3] = 255;
    }
    else
    {
        for (unsigned channel = 0; channel < 3; ++channel)
            colors[2][channel] = static_cast<std::uint8_t>((colors[0][channel] + colors[1][channel]) / 2);
        colors[2][3] = 255;
        colors[3] = {0, 0, 0, 0};
    }
    const std::uint32_t indices = Read<std::uint32_t>(std::span(block, 8), 4);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            StorePixel(image, blockX * 4 + x, blockY * 4 + y,
                       colors[(indices >> (2 * (y * 4 + x))) & 3]);
}

std::array<std::uint8_t, 16> DecodeDxt5Alpha(const std::uint8_t *block)
{
    std::array<std::uint8_t, 8> table{};
    table[0] = block[0];
    table[1] = block[1];
    if (table[0] > table[1])
        for (unsigned index = 1; index <= 6; ++index)
            table[index + 1] = static_cast<std::uint8_t>(((7 - index) * table[0] + index * table[1]) / 7);
    else
    {
        for (unsigned index = 1; index <= 4; ++index)
            table[index + 1] = static_cast<std::uint8_t>(((5 - index) * table[0] + index * table[1]) / 5);
        table[6] = 0;
        table[7] = 255;
    }
    std::uint64_t bits = 0;
    for (unsigned index = 0; index < 6; ++index)
        bits |= static_cast<std::uint64_t>(block[index + 2]) << (index * 8);
    std::array<std::uint8_t, 16> alpha{};
    for (unsigned index = 0; index < 16; ++index)
        alpha[index] = table[(bits >> (index * 3)) & 7];
    return alpha;
}

unsigned MaskShift(const std::uint32_t mask)
{
    if (!mask)
        return 0;
    unsigned shift = 0;
    while (((mask >> shift) & 1) == 0)
        ++shift;
    return shift;
}

unsigned MaskBits(std::uint32_t mask)
{
    unsigned bits = 0;
    while (mask)
    {
        bits += mask & 1;
        mask >>= 1;
    }
    return bits;
}

std::uint8_t ExtractChannel(const std::uint32_t value, const std::uint32_t mask,
                            const std::uint8_t fallback)
{
    if (!mask)
        return fallback;
    const unsigned shift = MaskShift(mask);
    const unsigned bits = MaskBits(mask);
    const std::uint32_t sample = (value & mask) >> shift;
    const std::uint32_t maximum = (1u << bits) - 1u;
    return static_cast<std::uint8_t>((sample * 255u + maximum / 2u) / maximum);
}

std::size_t DdsLevelSize(const unsigned width, const unsigned height, const std::uint32_t fourCC,
                         const unsigned bits)
{
    if (fourCC == 0x31545844u)
        return static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * 8;
    if (fourCC == 0x33545844u || fourCC == 0x35545844u)
        return static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * 16;
    return (static_cast<std::size_t>(width) * height * bits + 7) / 8;
}

std::vector<Image> DecodeDds(const std::filesystem::path &path)
{
    const auto bytes = ReadBytes(path);
    const std::span<const std::uint8_t> data(bytes);
    if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) != 0 || Read<std::uint32_t>(data, 4) != 124)
        throw std::runtime_error("invalid DDS image " + path.string());
    const unsigned height = Read<std::uint32_t>(data, 12);
    const unsigned width = Read<std::uint32_t>(data, 16);
    const unsigned mipCount = std::max(1u, Read<std::uint32_t>(data, 28));
    const std::uint32_t fourCC = Read<std::uint32_t>(data, 84);
    const unsigned bits = Read<std::uint32_t>(data, 88);
    const std::array<std::uint32_t, 4> masks{Read<std::uint32_t>(data, 92), Read<std::uint32_t>(data, 96),
                                             Read<std::uint32_t>(data, 100), Read<std::uint32_t>(data, 104)};
    const std::uint32_t caps2 = Read<std::uint32_t>(data, 112);
    if (!width || !height || width > 8192 || height > 8192 || mipCount > 14 ||
        (fourCC && fourCC != 0x31545844u && fourCC != 0x33545844u && fourCC != 0x35545844u))
        throw std::runtime_error("unsupported DDS image " + path.string());
    const unsigned faces = (caps2 & 0x200u) ? 6u : 1u;
    if (faces == 6 && (caps2 & 0xFC00u) != 0xFC00u)
        throw std::runtime_error("incomplete DDS cubemap " + path.string());

    std::size_t offset = 128;
    std::vector<Image> result;
    result.reserve(faces);
    for (unsigned face = 0; face < faces; ++face)
    {
        const std::size_t topSize = DdsLevelSize(width, height, fourCC, bits);
        if (offset > bytes.size() || topSize > bytes.size() - offset)
            throw std::runtime_error("truncated DDS pixels " + path.string());
        Image image{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
        const std::uint8_t *source = bytes.data() + offset;
        if (fourCC)
        {
            const unsigned blockSize = fourCC == 0x31545844u ? 8u : 16u;
            for (unsigned by = 0; by < (height + 3) / 4; ++by)
                for (unsigned bx = 0; bx < (width + 3) / 4; ++bx)
                {
                    const std::uint8_t *block = source + (static_cast<std::size_t>(by) * ((width + 3) / 4) + bx) * blockSize;
                    DecodeColorBlock(image, bx, by, block + (blockSize - 8), fourCC == 0x31545844u);
                    if (fourCC == 0x33545844u)
                    {
                        std::uint64_t alpha = Read<std::uint64_t>(std::span(block, 16), 0);
                        for (unsigned index = 0; index < 16; ++index)
                        {
                            const unsigned x = bx * 4 + index % 4, y = by * 4 + index / 4;
                            if (x < width && y < height)
                                image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] =
                                    static_cast<std::uint8_t>(((alpha >> (index * 4)) & 15) * 17);
                        }
                    }
                    else if (fourCC == 0x35545844u)
                    {
                        const auto alpha = DecodeDxt5Alpha(block);
                        for (unsigned index = 0; index < 16; ++index)
                        {
                            const unsigned x = bx * 4 + index % 4, y = by * 4 + index / 4;
                            if (x < width && y < height)
                                image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = alpha[index];
                        }
                    }
                }
        }
        else if (bits == 8)
        {
            for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
                image.rgba[index * 4 + 0] = image.rgba[index * 4 + 1] = image.rgba[index * 4 + 2] = source[index],
                image.rgba[index * 4 + 3] = 255;
        }
        else if (bits == 24 || bits == 32)
        {
            const unsigned stride = bits / 8;
            for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, source + index * stride, stride);
                for (unsigned channel = 0; channel < 4; ++channel)
                    image.rgba[index * 4 + channel] = ExtractChannel(value, masks[channel], channel == 3 ? 255 : 0);
            }
        }
        else
            throw std::runtime_error("unsupported uncompressed DDS image " + path.string());
        result.push_back(std::move(image));
        for (unsigned level = 0, w = width, h = height; level < mipCount; ++level)
        {
            offset += DdsLevelSize(w, h, fourCC, bits);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
    }
    return result;
}

Image DecodeDdsLevel(const std::span<const std::uint8_t> source, const unsigned width,
                     const unsigned height, const std::uint32_t fourCC, const unsigned bits,
                     const std::array<std::uint32_t, 4> &masks)
{
    const std::size_t required = DdsLevelSize(width, height, fourCC, bits);
    if (source.size() != required)
        throw std::runtime_error("invalid DDS mip payload");
    Image image{width, height,
                std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    if (fourCC)
    {
        const unsigned blockSize = fourCC == 0x31545844u ? 8u : 16u;
        for (unsigned blockY = 0; blockY < (height + 3) / 4; ++blockY)
            for (unsigned blockX = 0; blockX < (width + 3) / 4; ++blockX)
            {
                const auto *block = source.data() +
                                    (static_cast<std::size_t>(blockY) * ((width + 3) / 4) +
                                     blockX) *
                                        blockSize;
                DecodeColorBlock(image, blockX, blockY, block + (blockSize - 8),
                                 fourCC == 0x31545844u);
                if (fourCC == 0x33545844u)
                {
                    const std::uint64_t alpha =
                        Read<std::uint64_t>(std::span(block, blockSize), 0);
                    for (unsigned index = 0; index < 16; ++index)
                    {
                        const unsigned x = blockX * 4 + index % 4;
                        const unsigned y = blockY * 4 + index / 4;
                        if (x < width && y < height)
                            image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] =
                                static_cast<std::uint8_t>(((alpha >> (index * 4)) & 15) * 17);
                    }
                }
                else if (fourCC == 0x35545844u)
                {
                    const auto alpha = DecodeDxt5Alpha(block);
                    for (unsigned index = 0; index < 16; ++index)
                    {
                        const unsigned x = blockX * 4 + index % 4;
                        const unsigned y = blockY * 4 + index / 4;
                        if (x < width && y < height)
                            image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] =
                                alpha[index];
                    }
                }
            }
    }
    else if (bits == 8)
    {
        for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
        {
            image.rgba[index * 4 + 0] = source[index];
            image.rgba[index * 4 + 1] = source[index];
            image.rgba[index * 4 + 2] = source[index];
            image.rgba[index * 4 + 3] = 255;
        }
    }
    else if (bits == 24 || bits == 32)
    {
        const unsigned stride = bits / 8;
        for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, source.data() + index * stride, stride);
            for (unsigned channel = 0; channel < 4; ++channel)
                image.rgba[index * 4 + channel] =
                    ExtractChannel(value, masks[channel], channel == 3 ? 255 : 0);
        }
    }
    else
    {
        throw std::runtime_error("unsupported uncompressed DDS mip");
    }
    return image;
}

Cubemap DecodeDdsCubemap(const std::filesystem::path &path)
{
    const auto bytes = ReadBytes(path);
    const std::span<const std::uint8_t> data(bytes);
    if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4) != 0 ||
        Read<std::uint32_t>(data, 4) != 124)
        throw std::runtime_error("invalid reflection-probe DDS " + path.string());
    const unsigned height = Read<std::uint32_t>(data, 12);
    const unsigned width = Read<std::uint32_t>(data, 16);
    const unsigned mipCount = std::max(1u, Read<std::uint32_t>(data, 28));
    const std::uint32_t fourCC = Read<std::uint32_t>(data, 84);
    const unsigned bits = Read<std::uint32_t>(data, 88);
    const std::array<std::uint32_t, 4> masks{
        Read<std::uint32_t>(data, 92), Read<std::uint32_t>(data, 96),
        Read<std::uint32_t>(data, 100), Read<std::uint32_t>(data, 104)};
    const std::uint32_t caps2 = Read<std::uint32_t>(data, 112);
    if (!width || width != height || width > 4096 || mipCount > 13 ||
        (caps2 & 0xFE00u) != 0xFE00u ||
        (fourCC && fourCC != 0x31545844u && fourCC != 0x33545844u &&
         fourCC != 0x35545844u))
        throw std::runtime_error("unsupported reflection-probe DDS " + path.string());
    unsigned terminalWidth = width;
    unsigned terminalHeight = height;
    for (unsigned level = 1; level < mipCount; ++level)
    {
        if (terminalWidth == 1 && terminalHeight == 1)
            throw std::runtime_error("reflection-probe DDS has too many mips " + path.string());
        terminalWidth = std::max(1u, terminalWidth / 2);
        terminalHeight = std::max(1u, terminalHeight / 2);
    }

    Cubemap result{width, height, mipCount, std::vector<std::vector<Image>>(6), {}};
    for (auto &face : result.faces)
        face.reserve(mipCount);
    std::size_t offset = 128;
    for (unsigned face = 0; face < 6; ++face)
    {
        unsigned mipWidth = width;
        unsigned mipHeight = height;
        for (unsigned level = 0; level < mipCount; ++level)
        {
            const std::size_t size = DdsLevelSize(mipWidth, mipHeight, fourCC, bits);
            if (offset > bytes.size() || size > bytes.size() - offset)
                throw std::runtime_error("truncated reflection-probe DDS " + path.string());
            result.faces[face].push_back(
                DecodeDdsLevel(data.subspan(offset, size), mipWidth, mipHeight, fourCC, bits,
                               masks));
            offset += size;
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);
        }
    }
    if (offset != bytes.size())
        throw std::runtime_error("reflection-probe DDS contains trailing data " + path.string());
    for (unsigned level = 0; level < mipCount; ++level)
        for (unsigned face = 0; face < 6; ++face)
            result.residentPixels.insert(result.residentPixels.end(),
                                         result.faces[face][level].rgba.begin(),
                                         result.faces[face][level].rgba.end());
    return result;
}

std::vector<Image> DecodeIwi(const std::filesystem::path &path)
{
    const auto bytes = ReadBytes(path);
    const std::span<const std::uint8_t> data(bytes);
    if (bytes.size() < 28 || std::memcmp(bytes.data(), "IWi\x06", 4) != 0)
        throw std::runtime_error("invalid IW3 IWI image " + path.string());
    const unsigned format = bytes[4], flags = bytes[5];
    const unsigned width = Read<std::uint16_t>(data, 6), height = Read<std::uint16_t>(data, 8);
    const unsigned depth = Read<std::uint16_t>(data, 10);
    if (!width || !height || width > 4096 || height > 4096 || (flags & 8) || depth > 1 ||
        format < 1 || format > 13 || (format >= 6 && format <= 10))
        throw std::runtime_error("unsupported IW3 IWI image " + path.string());
    const unsigned faces = flags & 4 ? 6u : 1u;
    const unsigned bytesPerPixel = format == 1 ? 4u : format == 2 ? 3u : format == 3 ? 2u : 1u;
    const std::uint32_t fourCC = format == 11 ? 0x31545844u
                                 : format == 12 ? 0x33545844u
                                                : format == 13 ? 0x35545844u : 0u;
    const std::size_t faceSize = fourCC ? DdsLevelSize(width, height, fourCC, 0)
                                        : static_cast<std::size_t>(width) * height * bytesPerPixel;
    if (faceSize > bytes.size() || faceSize * faces > bytes.size() - 28 ||
        Read<std::uint32_t>(data, 12) != bytes.size())
        throw std::runtime_error("truncated IW3 IWI image " + path.string());
    const std::size_t start = bytes.size() - faceSize * faces;
    std::vector<Image> result;
    result.reserve(faces);
    for (unsigned face = 0; face < faces; ++face)
    {
        const std::uint8_t *source = bytes.data() + start + face * faceSize;
        Image image{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
        if (fourCC)
        {
            const unsigned blockSize = fourCC == 0x31545844u ? 8u : 16u;
            for (unsigned by = 0; by < (height + 3) / 4; ++by)
                for (unsigned bx = 0; bx < (width + 3) / 4; ++bx)
                {
                    const std::uint8_t *block = source + (static_cast<std::size_t>(by) * ((width + 3) / 4) + bx) * blockSize;
                    DecodeColorBlock(image, bx, by, block + (blockSize - 8), fourCC == 0x31545844u);
                    if (fourCC == 0x33545844u)
                    {
                        const std::uint64_t alpha = Read<std::uint64_t>(std::span(block, 16), 0);
                        for (unsigned index = 0; index < 16; ++index)
                        {
                            const unsigned x = bx * 4 + index % 4, y = by * 4 + index / 4;
                            if (x < width && y < height)
                                image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] =
                                    static_cast<std::uint8_t>(((alpha >> (index * 4)) & 15) * 17);
                        }
                    }
                    else if (fourCC == 0x35545844u)
                    {
                        const auto alpha = DecodeDxt5Alpha(block);
                        for (unsigned index = 0; index < 16; ++index)
                        {
                            const unsigned x = bx * 4 + index % 4, y = by * 4 + index / 4;
                            if (x < width && y < height)
                                image.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = alpha[index];
                        }
                    }
                }
        }
        else
        {
            for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
            {
                const std::uint8_t *pixel = source + index * bytesPerPixel;
                auto *target = image.rgba.data() + index * 4;
                if (format == 1)
                    target[0] = pixel[2], target[1] = pixel[1], target[2] = pixel[0], target[3] = pixel[3];
                else if (format == 2)
                    target[0] = pixel[2], target[1] = pixel[1], target[2] = pixel[0], target[3] = 255;
                else if (format == 3)
                    target[0] = target[1] = target[2] = pixel[0], target[3] = pixel[1];
                else if (format == 4)
                    target[0] = target[1] = target[2] = pixel[0], target[3] = 255;
                else
                    target[0] = target[1] = target[2] = 255, target[3] = pixel[0];
            }
        }
        result.push_back(std::move(image));
    }
    return result;
}

Image SolidImage(const std::array<std::uint8_t, 4> color)
{
    return {1, 1, {color[0], color[1], color[2], color[3]}};
}

std::vector<Image> LoadImage(const std::filesystem::path &root,
                             const std::vector<std::filesystem::path> &sourcePaths,
                             const std::string &name)
{
    if (name == "$white")
        return {SolidImage({255, 255, 255, 255})};
    if (name == "$black")
        return {SolidImage({0, 0, 0, 255})};
    if (name == "$identitynormalmap")
        return {SolidImage({128, 128, 255, 255})};
    const auto path = root / "images" / (name + ".dds");
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error))
        return DecodeDds(path);
    for (const auto &source : sourcePaths)
        for (const auto &candidate : {source / "images" / (name + ".iwi"),
                                      source / "raw" / "images" / (name + ".iwi")})
            if (std::filesystem::is_regular_file(candidate, error))
                return DecodeIwi(candidate);
    throw std::runtime_error("missing IW3 image '" + name +
                             "'; add the matching IW3 main and raw directories with --search-path");
}

float Linear(const float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float Srgb(const float value)
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::array<std::array<float, 9>, 4> ProjectReflectionSh(const Cubemap &cubemap)
{
    if (cubemap.faces.size() != 6 || cubemap.faces.front().empty())
        throw std::runtime_error("reflection probe has no cubemap faces");
    std::array<std::array<double, 9>, 4> accumulated{};
    double totalWeight = 0.0;
    for (unsigned face = 0; face < 6; ++face)
    {
        const Image &image = cubemap.faces[face].front();
        if (image.width != cubemap.width || image.height != cubemap.height)
            throw std::runtime_error("reflection-probe cubemap faces do not match");
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x)
            {
                const double u = 2.0 * (static_cast<double>(x) + 0.5) / image.width - 1.0;
                const double v = 2.0 * (static_cast<double>(y) + 0.5) / image.height - 1.0;
                std::array<double, 3> direction{};
                switch (face)
                {
                case 0:
                    direction = {1.0, -v, -u};
                    break;
                case 1:
                    direction = {-1.0, -v, u};
                    break;
                case 2:
                    direction = {u, 1.0, v};
                    break;
                case 3:
                    direction = {u, -1.0, -v};
                    break;
                case 4:
                    direction = {u, -v, 1.0};
                    break;
                default:
                    direction = {-u, -v, -1.0};
                    break;
                }
                const double length = std::sqrt(direction[0] * direction[0] +
                                                direction[1] * direction[1] +
                                                direction[2] * direction[2]);
                for (double &component : direction)
                    component /= length;
                const double dx = direction[0], dy = direction[1], dz = direction[2];
                const std::array<double, 9> basis{
                    0.282095,
                    0.488603 * dy,
                    0.488603 * dz,
                    0.488603 * dx,
                    1.092548 * dx * dy,
                    1.092548 * dy * dz,
                    0.315392 * (3.0 * dz * dz - 1.0),
                    1.092548 * dx * dz,
                    0.546274 * (dx * dx - dy * dy)};
                const double weight = 1.0 / std::pow(1.0 + u * u + v * v, 1.5);
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * image.width + x) * 4;
                const std::array<double, 4> sample{
                    Linear(image.rgba[offset + 0] / 255.0f),
                    Linear(image.rgba[offset + 1] / 255.0f),
                    Linear(image.rgba[offset + 2] / 255.0f),
                    image.rgba[offset + 3] / 255.0};
                for (unsigned channel = 0; channel < 4; ++channel)
                    for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
                        accumulated[channel][coefficient] +=
                            sample[channel] * basis[coefficient] * weight;
                totalWeight += weight;
            }
    }
    if (!(totalWeight > 0.0) || !std::isfinite(totalWeight))
        throw std::runtime_error("reflection-probe SH projection has invalid weight");
    constexpr double sphereArea = 12.56637061435917295385;
    std::array<std::array<float, 9>, 4> result{};
    for (unsigned channel = 0; channel < 4; ++channel)
        for (unsigned coefficient = 0; coefficient < 9; ++coefficient)
        {
            const double value = accumulated[channel][coefficient] * sphereArea / totalWeight;
            if (!std::isfinite(value))
                throw std::runtime_error("reflection-probe SH projection is non-finite");
            result[channel][coefficient] = static_cast<float>(value);
        }
    return result;
}

Image Resize(const Image &source, const unsigned width, const unsigned height,
             const bool color, const std::array<float, 4> &tint = {1, 1, 1, 1})
{
    Image output{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (unsigned y = 0; y < height; ++y)
    {
        const float sy = (static_cast<float>(y) + 0.5f) * source.height / height - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, static_cast<int>(source.height) - 1);
        const int y1 = std::min(y0 + 1, static_cast<int>(source.height) - 1);
        const float fy = std::clamp(sy - std::floor(sy), 0.0f, 1.0f);
        for (unsigned x = 0; x < width; ++x)
        {
            const float sx = (static_cast<float>(x) + 0.5f) * source.width / width - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, static_cast<int>(source.width) - 1);
            const int x1 = std::min(x0 + 1, static_cast<int>(source.width) - 1);
            const float fx = std::clamp(sx - std::floor(sx), 0.0f, 1.0f);
            float sample[4]{};
            for (unsigned iy = 0; iy < 2; ++iy)
                for (unsigned ix = 0; ix < 2; ++ix)
                {
                    const float weight = (ix ? fx : 1 - fx) * (iy ? fy : 1 - fy);
                    const std::size_t offset = (static_cast<std::size_t>(iy ? y1 : y0) * source.width +
                                                static_cast<unsigned>(ix ? x1 : x0)) * 4;
                    const float alpha = source.rgba[offset + 3] / 255.0f;
                    for (unsigned channel = 0; channel < 4; ++channel)
                    {
                        float value = source.rgba[offset + channel] / 255.0f;
                        if (color && channel < 3)
                            value = Linear(value) * alpha;
                        sample[channel] += value * weight;
                    }
                }
            const float alpha = std::clamp(sample[3] * tint[3], 0.0f, 1.0f);
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                float value = sample[channel];
                if (color && channel < 3)
                    value = Srgb(std::clamp((sample[3] > 1.0e-6f ? value / sample[3] : 0.0f) * tint[channel], 0.0f, 1.0f));
                else if (channel == 3)
                    value = alpha;
                output.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + channel] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
            }
        }
    }
    return output;
}

void Paste(Image &target, const Image &source, const unsigned x, const unsigned y)
{
    if (x > target.width || y > target.height || source.width > target.width - x ||
        source.height > target.height - y)
        throw std::runtime_error("IW3 atlas write exceeds its dimensions");
    for (unsigned row = 0; row < source.height; ++row)
        std::memcpy(target.rgba.data() + (static_cast<std::size_t>(y + row) * target.width + x) * 4,
                    source.rgba.data() + static_cast<std::size_t>(row) * source.width * 4,
                    static_cast<std::size_t>(source.width) * 4);
}

template <std::size_t Size>
std::array<float, Size> Literal(const Json &value, const std::array<float, Size> &fallback)
{
    if (!value.is_array() || value.size() != Size)
        return fallback;
    std::array<float, Size> result{};
    for (std::size_t index = 0; index < Size; ++index)
    {
        result[index] = value.at(index).get<float>();
        if (!std::isfinite(result[index]) || result[index] < 0 || result[index] > 64)
            throw std::runtime_error("invalid IW3 material constant");
    }
    return result;
}

SourceMaterial ReadMaterial(const std::filesystem::path &root, const std::string &name)
{
    if (name.empty() || name.find("..") != std::string::npos || name.find(':') != std::string::npos ||
        name.find('\\') != std::string::npos || name.front() == '/')
        throw std::runtime_error("invalid IW3 material name");
    const auto path = root / "materials" / (name + ".json");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        throw std::runtime_error("missing IW3 material '" + name +
                                 "'; add the matching IW3 main and raw directories with --search-path");
    const Json source = ReadJson(path);
    SourceMaterial material;
    material.name = name;
    const std::string techset = source.at("techniqueSet").get<std::string>();
    if (techset == "shadowcaster" || std::regex_search(techset, std::regex("(^|_)sky($|_)", std::regex::icase)))
    {
        material.kind = SurfaceKind::skipped;
        return material;
    }
    bool authoredAlpha = std::regex_search(techset, std::regex("(^|_)[at][0-9]", std::regex::icase)) ||
                         techset.find("alphatest") != std::string::npos;
    const bool blended = std::regex_search(techset, std::regex("(^|_)b[0-9]", std::regex::icase));
    std::string alphaMode = "ge128";
    for (const auto &texture : source.value("textures", Json::array()))
    {
        const std::string semantic = texture.value("semantic", std::string{});
        if (semantic == "colorMap")
            material.color = texture.at("image").get<std::string>();
        else if (semantic == "normalMap")
            material.normal = texture.at("image").get<std::string>();
        else if (semantic == "specularMap")
            material.response = texture.at("image").get<std::string>();
    }
    for (const auto &constant : source.value("constants", Json::array()))
    {
        const std::string key = constant.value("name", std::string{});
        if (key == "colorTint")
            material.tint = Literal<4>(constant.at("literal"), material.tint);
        else if (key == "envMapParms")
            material.environment = Literal<4>(constant.at("literal"), material.environment);
    }
    for (const auto &state : source.value("stateBits", Json::array()))
    {
        const std::string test = state.value("alphaTest", std::string{});
        if (test == "gt0" || test == "lt128" || test == "ge128")
        {
            authoredAlpha = true;
            if (state.value("colorWriteRgb", false) && !state.value("polymodeLine", false))
                alphaMode = test;
        }
    }
    if (material.color.empty())
        throw std::runtime_error("IW3 material has no color map: " + name);
    material.kind = blended ? SurfaceKind::glass : (authoredAlpha ? SurfaceKind::cutout : SurfaceKind::opaque);
    if (material.kind == SurfaceKind::cutout)
        material.flags = alphaMode == "gt0" ? 8u : (alphaMode == "lt128" ? 16u : 0u);
    if (!material.normal.empty())
        material.flags |= 32u;
    if (!material.response.empty())
        material.flags |= 64u;
    return material;
}

bool FoliageName(const std::string &value)
{
    return std::regex_search(value, std::regex("leaf|leaves|foliage|tree|bush|branch|grass|fern|pine|hedge|palm",
                                               std::regex::icase));
}

std::string TileKey(const SourceMaterial &material)
{
    std::string key = material.color + '\x1f' + material.normal + '\x1f' + material.response;
    for (const float value : material.tint)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        key.append(reinterpret_cast<const char *>(&bits), sizeof(bits));
    }
    return key;
}

void ClassifyFoliage(SourceMaterial &material, const Image &color)
{
    if (material.kind != SurfaceKind::opaque || !FoliageName(material.name + " " + material.color))
        return;
    std::size_t low = 0, high = 0;
    for (std::size_t offset = 3; offset < color.rgba.size(); offset += 4)
        color.rgba[offset] < 128 ? ++low : ++high;
    const std::size_t threshold = color.rgba.size() / 4 / 1000;
    if (low > threshold && high > threshold)
        material.kind = SurfaceKind::cutout;
}

std::vector<std::uint8_t> Sha256(const std::span<const std::uint8_t> data)
{
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectLength = 0, digestLength = 0, returned = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &returned, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestLength),
                          sizeof(digestLength), &returned, 0) < 0)
        throw std::runtime_error("cannot initialize SHA-256");
    std::vector<std::uint8_t> object(objectLength), digest(digestLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), digestLength, 0) < 0)
    {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("cannot calculate SHA-256");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

std::string Hex(const std::span<const std::uint8_t> data)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output(data.size() * 2, '0');
    for (std::size_t index = 0; index < data.size(); ++index)
    {
        output[index * 2] = digits[data[index] >> 4];
        output[index * 2 + 1] = digits[data[index] & 15];
    }
    return output;
}

std::vector<std::uint8_t> Unhex(const std::string &text)
{
    if (text.size() % 2)
        throw std::runtime_error("invalid embedded technique hex");
    std::vector<std::uint8_t> output(text.size() / 2);
    auto digit = [](const char value) -> unsigned {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        throw std::runtime_error("invalid embedded technique hex digit");
    };
    for (std::size_t index = 0; index < output.size(); ++index)
        output[index] = static_cast<std::uint8_t>((digit(text[index * 2]) << 4) | digit(text[index * 2 + 1]));
    return output;
}

template <class T> T At(const std::vector<std::uint8_t> &data, const std::size_t offset)
{
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("invalid technique template");
    T result{};
    std::memcpy(&result, data.data() + offset, sizeof(result));
    return result;
}

template <class T> void Put(std::vector<std::uint8_t> &data, const std::size_t offset, const T value)
{
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("invalid technique template");
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<std::uint8_t> Compile(const std::string &source, const char *target, const char *name)
{
    ID3DBlob *program = nullptr, *errors = nullptr;
    const HRESULT result = D3DCompile(source.data(), source.size(), name, nullptr, nullptr, "main", target,
                                      D3DCOMPILE_ENABLE_STRICTNESS, 0, &program, &errors);
    std::string message;
    if (errors)
    {
        message.assign(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
        errors->Release();
    }
    if (FAILED(result) || !program)
        throw std::runtime_error("cannot compile IW3 Replay shader: " + message);
    const auto *begin = static_cast<const std::uint8_t *>(program->GetBufferPointer());
    std::vector<std::uint8_t> output(begin, begin + program->GetBufferSize());
    program->Release();
    if (output.size() < 4 || std::memcmp(output.data(), "DXBC", 4) != 0)
        throw std::runtime_error("D3D compiler returned an invalid shader");
    return output;
}

Json Shader(const unsigned type, const std::string &label, const std::string &debugName,
            const std::vector<std::uint8_t> &program)
{
    const auto digest = Sha256(program);
    const std::string name = "mw120r_" + label + "_" + Hex(std::span(digest).first(12));
    std::array<std::uint8_t, 40> header{};
    const std::uint32_t length = static_cast<std::uint32_t>(program.size());
    std::uint32_t identity{};
    std::memcpy(&identity, digest.data(), sizeof(identity));
    std::memcpy(header.data() + 32, &length, sizeof(length));
    std::memcpy(header.data() + 36, &identity, sizeof(identity));
    return {{"type", type}, {"name", name}, {"debugName", debugName},
            {"header", Hex(header)}, {"program", Hex(program)}};
}

void PatchStateIdentity(Json &technique, const std::vector<std::uint8_t> &seed)
{
    auto states = Unhex(technique.at("states").get<std::string>());
    const auto header = Unhex(technique.at("header").get<std::string>());
    for (std::size_t offset = 0; offset < states.size(); offset += 16)
    {
        std::vector<std::uint8_t> input = seed;
        input.insert(input.end(), header.begin(), header.end());
        input.push_back(static_cast<std::uint8_t>(offset));
        const auto digest = Sha256(input);
        std::copy_n(digest.begin(), 8, states.begin() + offset);
    }
    technique["states"] = Hex(states);
}

std::string CoveragePrefix(const std::string &source, const std::string &signature)
{
    const std::string original = "float4 main(Input input) : SV_TARGET0";
    const std::size_t start = source.find(original);
    const std::size_t clip = source.find("clip(texel.a - .5);", start);
    if (start == std::string::npos || clip == std::string::npos)
        throw std::runtime_error("embedded IW3 shader is missing alpha coverage");
    return source.substr(0, start) + signature + source.substr(start + original.size(),
                                                               clip + std::strlen("clip(texel.a - .5);") -
                                                                   (start + original.size()));
}

std::string PrepassSource(const std::string &source)
{
    return CoveragePrefix(source, "float main(Input input) : SV_TARGET1") + R"(
    float3 normal = normalize(input.normal);
    uint3 packed = (uint3)(saturate(normal * .5 + .5) * 1023.0);
    return asfloat(packed.x | (packed.y << 10) | (packed.z << 20));
}
)";
}

std::string ShadowSource(const std::string &source)
{
    std::string output = CoveragePrefix(source, "void main(Input input)");
    const std::string key = "uint kind = flags % 4;";
    const std::size_t where = output.find(key);
    if (where == std::string::npos)
        throw std::runtime_error("embedded IW3 shader is missing its material kind");
    output.insert(where + key.size(), "\n    if (kind != 3) return;");
    output += "\n}\n";
    return output;
}

void AppendAtlasArgument(Json &technique)
{
    technique["args"] = technique.at("args").get<std::string>() + "051001000000";
}

void KeepReferencedShaders(Json &techset)
{
    std::set<std::string> needed;
    for (const auto &technique : techset.at("techniques"))
        for (unsigned index = 0; index < 4; ++index)
            if (!technique.at("shaders").at(index).is_null())
                needed.insert(std::to_string(14 + index) + ":" +
                              technique.at("shaders").at(index).get<std::string>());
    Json shaders = Json::object();
    for (const auto &key : needed)
        shaders[key] = techset.at("shaders").at(key);
    techset["shaders"] = std::move(shaders);
}

Json BuildTechset(const std::string &map, const unsigned columns)
{
    Json techset = Json::parse(ResourceText(IDR_IW3_TECHSET_TEMPLATE));
    std::string pixelSource = "#define MAP_SOURCE_SUN_MASK 1\n#define MAP_SOURCE_CHANNELS 1\n" +
                              ResourceText(IDR_IW3_PIXEL_SHADER);
    const std::string token = "ATLAS_COLUMNS";
    for (std::size_t at = pixelSource.find(token); at != std::string::npos;
         at = pixelSource.find(token, at + 1))
        pixelSource.replace(at, token.size(), std::to_string(columns));
    const auto pixel = Compile(pixelSource, "ps_5_0", "iw3_map_surface.hlsl");
    const auto vertex = Compile(ResourceText(IDR_IW3_VERTEX_SHADER), "vs_5_0", "iw3_world_vertex.hlsl");
    Json pixelShader = Shader(17, "iw3_surface", "iw3_map_surface.hlsl", pixel);
    Json vertexShader = Shader(14, "iw3_world", "iw3_world_vertex.hlsl", vertex);
    Json &lit = techset.at("techniques").back();
    const std::string oldPixel = lit.at("shaders").at(3).get<std::string>();
    techset.at("shaders").erase("17:" + oldPixel);
    lit["shaders"][3] = pixelShader.at("name");
    lit["shaders"][0] = vertexShader.at("name");
    techset["shaders"]["17:" + pixelShader.at("name").get<std::string>()] = pixelShader;
    techset["shaders"]["14:" + vertexShader.at("name").get<std::string>()] = vertexShader;
    const auto techDigest = Sha256(pixel);
    techset["name"] = "tw/mw120r_" + map + "_" + Hex(std::span(techDigest).first(6));
    lit["name"] = "TECHNIQUE_LIT_FORWARDPLUS_BITMASK_" + techset.at("name").get<std::string>().substr(3);
    PatchStateIdentity(lit, pixel);

    const auto depthProgram = Compile(PrepassSource(pixelSource), "ps_5_0", "iw3_depth_coverage.hlsl");
    Json depthShader = Shader(17, "iw3_depth", "iw3_depth_coverage.hlsl", depthProgram);
    Json &depth = techset.at("techniques").front();
    auto depthHeader = Unhex(depth.at("header").get<std::string>());
    if (At<std::uint32_t>(depthHeader, 8) != 0)
        throw std::runtime_error("embedded technique set has no depth prepass");
    depthHeader[0x9C] = 35;
    depthHeader[0x7A] = 4;
    depth["header"] = Hex(depthHeader);
    depth["shaders"] = Json::array({vertexShader.at("name"), nullptr, nullptr, depthShader.at("name")});
    depth["name"] = "TECHNIQUE_DEPTH_PREPASS_" + depthShader.at("name").get<std::string>();
    AppendAtlasArgument(depth);
    PatchStateIdentity(depth, depthProgram);
    techset["shaders"]["17:" + depthShader.at("name").get<std::string>()] = depthShader;

    const auto shadowProgram = Compile(ShadowSource(pixelSource), "ps_5_0", "iw3_shadow_coverage.hlsl");
    Json shadowShader = Shader(17, "iw3_shadow", "iw3_shadow_coverage.hlsl", shadowProgram);
    std::vector<unsigned> shadowTypes;
    for (auto &technique : techset.at("techniques"))
    {
        auto header = Unhex(technique.at("header").get<std::string>());
        const unsigned type = At<std::uint32_t>(header, 8);
        if (type != 27 && type != 28)
            continue;
        header[0x9C] = 35;
        header[0x7A] = 4;
        technique["header"] = Hex(header);
        technique["shaders"] = Json::array({vertexShader.at("name"), nullptr, nullptr,
                                              shadowShader.at("name")});
        technique["name"] = "TECHNIQUE_SHADOW_" + std::to_string(type) + "_" +
                            shadowShader.at("name").get<std::string>();
        AppendAtlasArgument(technique);
        PatchStateIdentity(technique, shadowProgram);
        shadowTypes.push_back(type);
    }
    std::sort(shadowTypes.begin(), shadowTypes.end());
    if (shadowTypes != std::vector<unsigned>{27, 28})
        throw std::runtime_error("embedded technique set has incomplete shadow passes");
    techset["shaders"]["17:" + shadowShader.at("name").get<std::string>()] = shadowShader;
    auto header = Unhex(techset.at("header").get<std::string>());
    Put<std::uint32_t>(header, 8, At<std::uint32_t>(header, 8) & ~0x1000u);
    techset["header"] = Hex(header);
    techset["coveragePrepass"] = true;
    techset["coverageShadows"] = true;
    KeepReferencedShaders(techset);
    return techset;
}

Json CreateVariant(const Json &base, Json &material, const std::string &stem,
                   const std::string &map, const std::string &kind)
{
    Json techset = base;
    Json lit = techset.at("techniques").back();
    auto header = Unhex(lit.at("header").get<std::string>());
    std::uint64_t other = At<std::uint64_t>(header, 0xA0);
    const bool foliage = kind == "foliage", glass = kind == "glass";
    other = (other & ~(0xE00ull | 3ull)) | (foliage ? 0x800ull : 0xC00ull);
    Put<std::uint64_t>(header, 0xA0, other);
    Put<std::uint64_t>(header, 0xA8, glass ? 0x28054ull : 0ull);
    lit["header"] = Hex(header);
    const auto identity = Sha256(header);
    techset["name"] = "tw/mw120r_" + map + "_" + kind + "_" + Hex(std::span(identity).first(6));
    lit["name"] = "TECHNIQUE_LIT_FORWARDPLUS_BITMASK_mw120r_" + map + "_" + kind;
    PatchStateIdentity(lit, header);
    Json techniques = Json::array();
    if (foliage)
    {
        Json depth = techset.at("techniques").front();
        auto depthHeader = Unhex(depth.at("header").get<std::string>());
        Put<std::uint64_t>(depthHeader, 0xA0, At<std::uint64_t>(depthHeader, 0xA0) & ~3ull);
        depth["header"] = Hex(depthHeader);
        PatchStateIdentity(depth, depthHeader);
        techniques.push_back(depth);
        for (const auto &source : techset.at("techniques"))
        {
            auto shadowHeader = Unhex(source.at("header").get<std::string>());
            const unsigned type = At<std::uint32_t>(shadowHeader, 8);
            if (type != 27 && type != 28)
                continue;
            Json shadow = source;
            Put<std::uint64_t>(shadowHeader, 0xA0, At<std::uint64_t>(shadowHeader, 0xA0) & ~3ull);
            shadow["header"] = Hex(shadowHeader);
            PatchStateIdentity(shadow, shadowHeader);
            techniques.push_back(shadow);
        }
    }
    techniques.push_back(lit);
    techset["techniques"] = std::move(techniques);
    auto techsetHeader = Unhex(techset.at("header").get<std::string>());
    std::uint64_t flags = At<std::uint64_t>(techsetHeader, 8) & ~(0x8080ull | 1ull);
    if (foliage)
        flags |= 0xA1;
    else if (glass)
        flags = (flags & ~0x20ull) | 1ull;
    Put<std::uint64_t>(techsetHeader, 8, flags);
    std::fill(techsetHeader.begin() + 24, techsetHeader.begin() + 56, std::uint8_t{0});
    for (const auto &technique : techset.at("techniques"))
    {
        const auto techniqueHeader = Unhex(technique.at("header").get<std::string>());
        const unsigned type = At<std::uint32_t>(techniqueHeader, 8);
        if (type >= 256)
            throw std::runtime_error("technique type exceeds the Replay mask");
        techsetHeader[24 + type / 8] |= static_cast<std::uint8_t>(1u << (type % 8));
    }
    techset["header"] = Hex(techsetHeader);
    KeepReferencedShaders(techset);
    const std::string materialFile = stem + "." + kind + ".material.json";
    const std::string techsetFile = stem + "." + kind + ".techset.json";
    Json variant = material;
    variant["techset"] = techset.at("name");
    variant["techsetDefinition"] = techsetFile;
    return {{"definition", materialFile}, {"techsetFile", techsetFile},
            {"techset", std::move(techset)}, {"material", std::move(variant)}};
}

Image Downsample(const Image &source, const bool color)
{
    const unsigned width = std::max(1u, source.width / 2), height = std::max(1u, source.height / 2);
    Image output{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            float sum[4]{};
            for (unsigned iy = 0; iy < 2; ++iy)
                for (unsigned ix = 0; ix < 2; ++ix)
                {
                    const unsigned sx = std::min(source.width - 1, x * 2 + ix);
                    const unsigned sy = std::min(source.height - 1, y * 2 + iy);
                    const std::size_t offset = (static_cast<std::size_t>(sy) * source.width + sx) * 4;
                    const float alpha = source.rgba[offset + 3] / 255.0f;
                    for (unsigned channel = 0; channel < 4; ++channel)
                    {
                        float value = source.rgba[offset + channel] / 255.0f;
                        if (color && channel < 3)
                            value = Linear(value) * alpha;
                        sum[channel] += value * 0.25f;
                    }
                }
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                float value = sum[channel];
                if (color && channel < 3)
                    value = Srgb(std::clamp(sum[3] > 1.0e-6f ? value / sum[3] : 0.0f, 0.0f, 1.0f));
                output.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + channel] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
            }
        }
    return output;
}

std::vector<std::uint8_t> MipChain(Image image, const unsigned levels, const bool color)
{
    std::vector<std::uint8_t> output;
    for (unsigned level = 0; level < levels; ++level)
    {
        output.insert(output.end(), image.rgba.begin(), image.rgba.end());
        if (level + 1 < levels)
            image = Downsample(image, color);
    }
    return output;
}

std::string ImageName(const std::string &map, const unsigned channel,
                      const std::vector<std::uint8_t> &pixels)
{
    const auto digest = Sha256(pixels);
    return "mw120r/" + map + "_" + std::to_string(channel) + "_" + Hex(std::span(digest).first(8));
}
} // namespace

RenderPlan PrepareRenderAssets(const std::filesystem::path &exportRoot, const Json &world,
                               const std::vector<std::string> &surfaceMaterials,
                               const std::vector<std::filesystem::path> &sourcePaths,
                               const std::filesystem::path &mapDirectory, const std::string &map)
{
    std::set<std::string> unique(surfaceMaterials.begin(), surfaceMaterials.end());
    std::vector<SourceMaterial> materials;
    materials.reserve(unique.size());
    std::unordered_map<std::string, std::vector<Image>> images;
    auto image = [&](const std::string &name) -> const std::vector<Image> & {
        auto found = images.find(name);
        if (found == images.end())
            found = images.emplace(name, LoadImage(exportRoot, sourcePaths, name)).first;
        return found->second;
    };
    for (const std::string &name : unique)
    {
        SourceMaterial material = ReadMaterial(exportRoot, name);
        if (material.kind != SurfaceKind::skipped)
        {
            const auto &color = image(material.color);
            if (color.size() != 1)
                throw std::runtime_error("IW3 material color map is a cubemap: " + name);
            ClassifyFoliage(material, color.front());
        }
        materials.push_back(std::move(material));
    }

    std::map<std::string, std::size_t> tileNumbers;
    for (const auto &material : materials)
        if (material.kind != SurfaceKind::skipped)
            tileNumbers.emplace(TileKey(material), 0);
    std::size_t nextTile = 0;
    const std::size_t visibleCount = tileNumbers.size();
    RenderPlan plan;
    const auto &sourceProbes = world.at("reflection_probes");
    if (!sourceProbes.is_array() || sourceProbes.empty() || sourceProbes.size() > 256)
        throw std::runtime_error("invalid IW3 reflection-probe table");
    plan.reflectionProbes.reserve(sourceProbes.size());
    for (std::size_t index = 0; index < sourceProbes.size(); ++index)
    {
        const auto &source = sourceProbes.at(index);
        const auto &sourceOrigin = source.at("origin");
        const std::string sourceName = source.at("image").get<std::string>();
        if (!sourceOrigin.is_array() || sourceOrigin.size() != 3 || sourceName.empty() ||
            sourceName.find("..") != std::string::npos || sourceName.find(':') != std::string::npos ||
            sourceName.find('\\') != std::string::npos || sourceName.front() == '/')
            throw std::runtime_error("invalid IW3 reflection probe");
        ReflectionProbePlan probe;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            probe.origin[axis] = sourceOrigin.at(axis).get<float>();
            if (!std::isfinite(probe.origin[axis]) || std::abs(probe.origin[axis]) > 100000.0f)
                throw std::runtime_error("IW3 reflection-probe origin is outside Replay range");
        }
        std::string sourceFile = sourceName;
        std::ranges::replace(sourceFile, '*', '_');
        const auto sourcePath = exportRoot / "images" / (sourceFile + ".dds");
        std::error_code error;
        if (!std::filesystem::is_regular_file(sourcePath, error))
            throw std::runtime_error("missing IW3 reflection-probe image '" + sourceName + "'");
        Cubemap cubemap = DecodeDdsCubemap(sourcePath);
        probe.sh = ProjectReflectionSh(cubemap);
        const std::string filename = map + "_reflection_probe_" + std::to_string(index) + ".rgba";
        const auto digest = Sha256(cubemap.residentPixels);
        const std::string targetName = "mw120r/" + map + "_reflection_probe_" +
                                       std::to_string(index) + "_" +
                                       Hex(std::span(digest).first(8));
        WriteBytes(mapDirectory / filename, cubemap.residentPixels);
        probe.image = {{"name", targetName},
                       {"width", cubemap.width},
                       {"height", cubemap.height},
                       {"rgba8", filename},
                       {"format", 7},
                       {"flags", 0x8001u},
                       {"depth", 1},
                       {"numElements", 1},
                       {"semantic", 1},
                       {"category", 1},
                       {"mipCount", cubemap.mipCount}};
        plan.reflectionProbes.push_back(std::move(probe));
    }
    std::vector<std::array<unsigned, 2>> lightmapDimensions;
    for (const auto &pair : world.value("lightmaps", Json::array()))
    {
        if (!pair.is_array() || pair.size() != 2 || pair.at(1).at("format") != 21 ||
            pair.at(1).at("height").get<unsigned>() % 2)
            throw std::runtime_error("unsupported IW3 lightmap format");
        lightmapDimensions.push_back(
            {pair.at(1).at("width"), pair.at(1).at("height").get<unsigned>() / 2});
    }
    unsigned columns = 0, cell = 0;
    std::vector<bool> occupied;
    for (const unsigned candidate : {4u, 8u, 16u, 32u})
    {
        const unsigned candidateCell = 4096 / candidate;
        std::vector<bool> candidateOccupied(static_cast<std::size_t>(candidate) * candidate);
        std::vector<LightmapRectangle> rectangles;
        bool fits = true;
        for (const auto &dimensions : lightmapDimensions)
        {
            const unsigned cellsWide = (dimensions[0] + candidateCell - 1) / candidateCell;
            const unsigned cellsHigh = (dimensions[1] + candidateCell - 1) / candidateCell;
            bool placed = false;
            if (cellsWide <= candidate && cellsHigh <= candidate)
                for (int row = static_cast<int>(candidate - cellsHigh); row >= 0 && !placed; --row)
                    for (unsigned column = 0; column + cellsWide <= candidate && !placed; ++column)
                    {
                        bool free = true;
                        for (unsigned y = 0; y < cellsHigh && free; ++y)
                            for (unsigned x = 0; x < cellsWide; ++x)
                                free &= !candidateOccupied[(row + y) * candidate + column + x];
                        if (!free)
                            continue;
                        for (unsigned y = 0; y < cellsHigh; ++y)
                            for (unsigned x = 0; x < cellsWide; ++x)
                                candidateOccupied[(row + y) * candidate + column + x] = true;
                        rectangles.push_back({column * candidateCell,
                                              static_cast<unsigned>(row) * candidateCell,
                                              dimensions[0], dimensions[1]});
                        placed = true;
                    }
            if (!placed)
            {
                fits = false;
                break;
            }
        }
        const std::size_t freeCells = std::count(candidateOccupied.begin(), candidateOccupied.end(), false);
        if (fits && freeCells >= visibleCount + 6)
        {
            columns = candidate;
            cell = candidateCell;
            occupied = std::move(candidateOccupied);
            plan.lightmaps = std::move(rectangles);
            break;
        }
    }
    if (!columns)
        throw std::runtime_error("IW3 material and lightmap data exceed the Replay atlas");
    std::vector<std::size_t> freeTiles;
    for (std::size_t index = 0; index < occupied.size(); ++index)
        if (!occupied[index])
            freeTiles.push_back(index);
    nextTile = 0;
    for (auto &[key, number] : tileNumbers)
        number = freeTiles[nextTile++];
    std::array<Image, 3> atlases{
        Image{4096, 4096, std::vector<std::uint8_t>(4096ull * 4096 * 4)},
        Image{4096, 4096, std::vector<std::uint8_t>(4096ull * 4096 * 4)},
        Image{4096, 4096, std::vector<std::uint8_t>(4096ull * 4096 * 4)}};

    plan.columns = columns;
    std::set<std::size_t> writtenTiles;
    for (const SourceMaterial &material : materials)
    {
        MaterialPlan item;
        item.kind = material.kind;
        item.flags = material.flags;
        item.environment = material.environment;
        if (material.kind != SurfaceKind::skipped)
        {
            item.tile = tileNumbers.at(TileKey(material));
            if (writtenTiles.insert(item.tile).second)
            {
                const Image color = Resize(image(material.color).front(), cell, cell, true, material.tint);
                Paste(atlases[0], color, static_cast<unsigned>(item.tile % columns) * cell,
                      static_cast<unsigned>(item.tile / columns) * cell);
                if (!material.normal.empty())
                    Paste(atlases[1], Resize(image(material.normal).front(), cell, cell, false),
                          static_cast<unsigned>(item.tile % columns) * cell,
                          static_cast<unsigned>(item.tile / columns) * cell);
                if (!material.response.empty())
                    Paste(atlases[2], Resize(image(material.response).front(), cell, cell, false),
                          static_cast<unsigned>(item.tile % columns) * cell,
                          static_cast<unsigned>(item.tile / columns) * cell);
            }
            plan.hasCutout |= item.kind == SurfaceKind::cutout;
            plan.hasGlass |= item.kind == SurfaceKind::glass;
        }
        plan.materials.emplace(material.name, item);
    }

    for (std::size_t face = 0; face < plan.skyTiles.size(); ++face)
        plan.skyTiles[face] = freeTiles[nextTile++];
    const std::string skyName = world.at("sky").get<std::string>();
    const auto &sky = image(skyName);
    if (sky.size() != 6)
        throw std::runtime_error("IW3 sky image is not a complete cubemap: " + skyName);
    for (unsigned face = 0; face < 6; ++face)
        Paste(atlases[0], Resize(sky[face], cell, cell, true),
              static_cast<unsigned>(plan.skyTiles[face] % columns) * cell,
              static_cast<unsigned>(plan.skyTiles[face] / columns) * cell);

    std::size_t lightmapIndex = 0;
    for (const auto &pair : world.value("lightmaps", Json::array()))
    {
        if (!pair.is_array() || pair.size() != 2 || pair.at(0).at("format") != 50 ||
            pair.at(1).at("format") != 21)
            throw std::runtime_error("unsupported IW3 lightmap format");
        const auto &primary = pair.at(0), &secondary = pair.at(1);
        const unsigned width = secondary.at("width"), fullHeight = secondary.at("height"), height = fullHeight / 2;
        if (!width || !height || fullHeight % 2)
            throw std::runtime_error("invalid IW3 lightmap dimensions");
        const auto firstBytes = ReadBytes(exportRoot / primary.at("file").get<std::string>());
        const auto secondBytes = ReadBytes(exportRoot / secondary.at("file").get<std::string>());
        if (firstBytes.size() != static_cast<std::size_t>(primary.at("width").get<unsigned>()) *
                                     primary.at("height").get<unsigned>() ||
            secondBytes.size() != static_cast<std::size_t>(width) * height * 8)
            throw std::runtime_error("invalid IW3 lightmap byte count");
        const auto &rectangle = plan.lightmaps.at(lightmapIndex++);
        Image primaryMask{primary.at("width"), primary.at("height"),
                          std::vector<std::uint8_t>(static_cast<std::size_t>(primary.at("width").get<unsigned>()) *
                                                    primary.at("height").get<unsigned>() * 4)};
        for (std::size_t index = 0; index < firstBytes.size(); ++index)
            primaryMask.rgba[index * 4 + 0] = primaryMask.rgba[index * 4 + 1] =
                primaryMask.rgba[index * 4 + 2] = primaryMask.rgba[index * 4 + 3] = firstBytes[index];
        const Image mask = Resize(primaryMask, width, height, false);
        Image color{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
        Image normal = color, response = color;
        const std::size_t half = static_cast<std::size_t>(width) * height * 4;
        for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
        {
            const auto *a = secondBytes.data() + index * 4;
            const auto *b = secondBytes.data() + half + index * 4;
            color.rgba[index * 4 + 0] = a[2];
            color.rgba[index * 4 + 1] = a[1];
            color.rgba[index * 4 + 2] = a[0];
            color.rgba[index * 4 + 3] = mask.rgba[index * 4];
            normal.rgba[index * 4 + 0] = a[3];
            normal.rgba[index * 4 + 1] = b[3];
            normal.rgba[index * 4 + 2] = a[2];
            normal.rgba[index * 4 + 3] = a[1];
            response.rgba[index * 4 + 0] = b[2];
            response.rgba[index * 4 + 1] = b[1];
            response.rgba[index * 4 + 2] = b[0];
            response.rgba[index * 4 + 3] = a[0];
        }
        Paste(atlases[0], color, rectangle.x, rectangle.y);
        Paste(atlases[1], normal, rectangle.x, rectangle.y);
        Paste(atlases[2], response, rectangle.x, rectangle.y);
    }

    const unsigned mipCount = static_cast<unsigned>(std::log2(cell)) - 1;
    Json material = Json::parse(ResourceText(IDR_IW3_MATERIAL_TEMPLATE));
    const std::string stem = map + ".d3dbsp";
    plan.material = "w/mw120r_" + map;
    plan.materialDefinition = stem + ".material.json";
    material["techsetDefinition"] = stem + ".techset.json";
    material["imageDefinitions"] = Json::array();
    for (unsigned channel = 0; channel < 3; ++channel)
    {
        auto chain = MipChain(std::move(atlases[channel]), mipCount, channel == 0);
        const std::string filename = map + "_atlas_" + std::to_string(channel) + ".rgba";
        const std::string name = ImageName(map, channel, chain);
        WriteBytes(mapDirectory / filename, chain);
        material["textures"][channel]["image"] = name;
        material["imageDefinitions"].push_back({{"name", name}, {"width", 4096}, {"height", 4096},
                                                  {"rgba8", filename}, {"format", channel == 0 ? 7 : 6},
                                                  {"mipCount", mipCount}});
    }
    Json techset = BuildTechset(map, columns);
    material["techset"] = techset.at("name");
    WriteJson(mapDirectory / plan.materialDefinition, material);
    WriteJson(mapDirectory / (stem + ".techset.json"), techset);
    for (const auto &[kind, enabled] : std::array<std::pair<const char *, bool>, 3>{
             std::pair{"foliage", plan.hasCutout}, std::pair{"glass", plan.hasGlass}, std::pair{"sky", true}})
    {
        if (!enabled)
            continue;
        Json variant = CreateVariant(techset, material, stem, map, kind);
        WriteJson(mapDirectory / variant.at("definition").get<std::string>(), variant.at("material"));
        WriteJson(mapDirectory / variant.at("techsetFile").get<std::string>(), variant.at("techset"));
        plan.additionalMaterials.push_back({{"schema", 1}, {"material", plan.material + "_" + kind},
                                             {"materialDefinition", variant.at("definition")}});
    }
    return plan;
}
} // namespace iw3
