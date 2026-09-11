#include "iw7_fastfile.h"

#include "common/fs_util.h"
#include "thirdparty/lz4/lz4.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace iw7
{
namespace
{
constexpr size_t kHeaderPrefixSize = 208;
constexpr size_t kHeaderSuffixSize = 24;
constexpr size_t kStreamFileSize = 24;
constexpr size_t kAuthBlockSize = 0x8000;
constexpr size_t kSecureDataSize = 0x800000;
constexpr size_t kRsaBlockSize = 0x4000;
constexpr uint32_t kVersion = 1619;
constexpr uint8_t kLz4Compression = 4;

#pragma pack(push, 1)
struct CompressionDataHeader
{
    uint64_t uncompressedSize;
    uint32_t blockSizeAndType;
};

struct CompressionBlockHeader
{
    uint32_t compressedSize;
    uint64_t uncompressedSize;
};
#pragma pack(pop)

static_assert(sizeof(CompressionDataHeader) == 12);
static_assert(sizeof(CompressionBlockHeader) == 12);

template <typename T> bool readValue(const std::vector<uint8_t> &bytes, const size_t offset, T &value)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
    {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return true;
}

bool compactSecurePayload(const std::vector<uint8_t> &file, const size_t payloadOffset,
                          const bool signedFile, std::vector<uint8_t> &payload,
                          std::string &error)
{
    if (payloadOffset >= file.size())
    {
        error = "fastfile has no compressed payload";
        return false;
    }

    if (!signedFile)
    {
        payload.assign(file.begin() + static_cast<ptrdiff_t>(payloadOffset), file.end());
        return true;
    }

    size_t source = payloadOffset;
    while (source < file.size())
    {
        const size_t count = (std::min)(kSecureDataSize, file.size() - source);
        payload.insert(payload.end(), file.begin() + static_cast<ptrdiff_t>(source),
                       file.begin() + static_cast<ptrdiff_t>(source + count));
        source += count;
        if (count != kSecureDataSize)
        {
            break;
        }
        if (file.size() - source < kRsaBlockSize)
        {
            error = "signed fastfile ends inside an RSA authentication block";
            return false;
        }
        source += kRsaBlockSize;
    }
    return true;
}

bool decompressPayload(const std::vector<uint8_t> &payload, FastfileInfo &info,
                       std::vector<uint8_t> &decompressed, std::string &error)
{
    if (payload.size() < 4 + sizeof(CompressionDataHeader) || payload[0] != 2 ||
        std::memcmp(payload.data() + 1, "IWC", 3) != 0)
    {
        error = "fastfile does not use IW7 block compression";
        return false;
    }

    CompressionDataHeader dataHeader{};
    std::memcpy(&dataHeader, payload.data() + 4, sizeof(dataHeader));
    info.uncompressedSize = dataHeader.uncompressedSize;
    info.blockSize = dataHeader.blockSizeAndType & 0x00FFFFFF;
    info.compressionType = static_cast<uint8_t>(dataHeader.blockSizeAndType >> 24);
    if (info.compressionType != kLz4Compression || info.blockSize == 0)
    {
        error = "unsupported IW7 compression type " + std::to_string(info.compressionType);
        return false;
    }
    if (info.uncompressedSize > std::numeric_limits<uint32_t>::max())
    {
        error = "IW7 payload exceeds the supported 4 GiB zone limit";
        return false;
    }

    decompressed.reserve(static_cast<size_t>(info.uncompressedSize));
    size_t cursor = 4 + sizeof(dataHeader);
    while (decompressed.size() < info.uncompressedSize)
    {
        CompressionBlockHeader block{};
        if (cursor > payload.size() || payload.size() - cursor < sizeof(block))
        {
            error = "truncated IW7 compression block header";
            return false;
        }
        std::memcpy(&block, payload.data() + cursor, sizeof(block));
        cursor += sizeof(block);

        if (block.compressedSize == 0 || block.uncompressedSize == 0 ||
            block.uncompressedSize > info.blockSize || block.uncompressedSize > INT_MAX ||
            block.compressedSize > INT_MAX || cursor > payload.size() ||
            payload.size() - cursor < block.compressedSize ||
            block.uncompressedSize > info.uncompressedSize - decompressed.size())
        {
            error = "invalid IW7 compression block sizes";
            return false;
        }

        const size_t destination = decompressed.size();
        decompressed.resize(destination + static_cast<size_t>(block.uncompressedSize));
        const int result = LZ4_decompress_safe(
            reinterpret_cast<const char *>(payload.data() + cursor),
            reinterpret_cast<char *>(decompressed.data() + destination),
            static_cast<int>(block.compressedSize), static_cast<int>(block.uncompressedSize));
        if (result != static_cast<int>(block.uncompressedSize))
        {
            error = "IW7 LZ4 block decompression failed";
            return false;
        }

        const size_t alignedSize = (static_cast<size_t>(block.compressedSize) + 3) & ~size_t{3};
        if (alignedSize > payload.size() - cursor)
        {
            error = "truncated IW7 compression block";
            return false;
        }
        cursor += alignedSize;
    }
    return true;
}
} // namespace

bool inspectFastfile(const std::string &path, FastfileInfo &info, std::string &error)
{
    info = {};
    error.clear();

    std::vector<uint8_t> file;
    if (!zt::read_file(path, file) || file.size() < kHeaderPrefixSize + kHeaderSuffixSize)
    {
        error = "cannot read IW7 fastfile";
        return false;
    }

    info.signedFile = std::memcmp(file.data(), "IWff0100", 8) == 0;
    if (!info.signedFile && std::memcmp(file.data(), "IWffu100", 8) != 0)
    {
        error = "invalid IW7 fastfile magic";
        return false;
    }
    if (!readValue(file, 8, info.version) || info.version != kVersion ||
        !readValue(file, 196, info.sharedStreamCount) ||
        !readValue(file, 204, info.imageStreamCount))
    {
        error = "unsupported or truncated IW7 fastfile header";
        return false;
    }

    const uint64_t streamCount =
        static_cast<uint64_t>(info.sharedStreamCount) + info.imageStreamCount;
    if (streamCount > (file.size() - kHeaderPrefixSize - kHeaderSuffixSize) / kStreamFileSize)
    {
        error = "IW7 stream table exceeds the fastfile";
        return false;
    }

    for (uint64_t index = 0; index < streamCount; ++index)
    {
        uint16_t packageIndex{};
        if (!readValue(file, kHeaderPrefixSize + static_cast<size_t>(index) * kStreamFileSize + 16,
                       packageIndex))
        {
            error = "truncated IW7 stream table";
            return false;
        }
        if (packageIndex != 0)
        {
            info.packageIndices.push_back(packageIndex);
        }
    }
    std::sort(info.packageIndices.begin(), info.packageIndices.end());
    info.packageIndices.erase(
        std::unique(info.packageIndices.begin(), info.packageIndices.end()),
        info.packageIndices.end());

    const size_t headerSize =
        kHeaderPrefixSize + static_cast<size_t>(streamCount) * kStreamFileSize + kHeaderSuffixSize;
    const size_t payloadOffset = headerSize + (info.signedFile ? kAuthBlockSize : 0);
    std::vector<uint8_t> payload;
    if (!compactSecurePayload(file, payloadOffset, info.signedFile, payload, error))
    {
        return false;
    }

    std::vector<uint8_t> decompressed;
    if (!decompressPayload(payload, info, decompressed, error) || decompressed.size() < 40)
    {
        if (error.empty())
        {
            error = "IW7 zone root is truncated";
        }
        return false;
    }

    std::memcpy(&info.scriptStringCount, decompressed.data(), sizeof(info.scriptStringCount));
    std::memcpy(&info.assetCount, decompressed.data() + 16, sizeof(info.assetCount));
    return true;
}

std::vector<std::string> requiredPackages(const FastfileInfo &info)
{
    std::vector<std::string> packages;
    packages.reserve(info.packageIndices.size());
    for (const uint16_t index : info.packageIndices)
    {
        packages.push_back("imagefile" + std::to_string(index) + ".pak");
    }
    return packages;
}
} // namespace iw7
