#include "ff_io.h"

#include "../zonetool/iw8/iw8_ffheader.h"
#include "fs_util.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zt
{
bool iw8_write(const std::string &outputPath, const std::vector<uint8_t> &zoneBody,
               const Iw8WriteParams &params)
{
    using namespace iw8ff;

    if (params.calcSize > params.totalDecompressed ||
        params.totalDecompressed - params.calcSize != zoneBody.size() ||
        zoneBody.size() > UINT32_MAX - 0xA0)
    {
        err("iw8: invalid body size or stream reservation for %s", outputPath.c_str());
        return false;
    }

    constexpr unsigned char storedFrame[] = {0x01, 0x49, 0x57, 0x43};
    std::vector<uint8_t> resident;
    resident.reserve(sizeof(storedFrame) + zoneBody.size());
    resident.insert(resident.end(), std::begin(storedFrame), std::end(storedFrame));
    resident.insert(resident.end(), zoneBody.begin(), zoneBody.end());

    IW8_DB_FFHeader header{};
    std::memcpy(header.magic, kMagicUnsec, sizeof(header.magic));
    header.headerVersion = kHeaderVersion;
    header.xfileVersion = kXFileVersion;
    header.dashCompressBuild = 0;
    header.dashEncryptBuild = 0;
    header.transientFileType = params.transientFileType;
    header.residentPartSize = static_cast<uint32_t>(resident.size());
    header.alwaysLoadedPartSize = static_cast<uint32_t>(zoneBody.size());
    header.xfileHeader.size = zoneBody.size();
    for (int index = 0; index < kNumStreams; ++index)
    {
        header.xfileHeader.blockSize[index] = params.blockSize[index];
    }

    const std::string directory = path_dir(outputPath);
    if (!directory.empty())
    {
        mkdirs(directory);
    }

    FILE *file = std::fopen(outputPath.c_str(), "wb");
    if (!file)
    {
        err("iw8: cannot open %s for writing", outputPath.c_str());
        return false;
    }

    constexpr size_t headerSize = 0x88;
    const bool written = std::fwrite(&header, 1, headerSize, file) == headerSize &&
                         std::fwrite(resident.data(), 1, resident.size(), file) == resident.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
    {
        err("iw8: failed to write %s", outputPath.c_str());
        return false;
    }

    info("iw8: wrote %s (%zu bytes)", outputPath.c_str(), headerSize + resident.size());
    return true;
}

bool inspect_ff(const std::string &fastfilePath)
{
    std::vector<uint8_t> bytes;
    if (!read_file(fastfilePath, bytes))
    {
        err("inspect: cannot read %s", fastfilePath.c_str());
        return false;
    }
    if (bytes.size() < 0x8C || std::memcmp(bytes.data(), iw8ff::kMagicUnsec, 8) != 0)
    {
        err("inspect: %s is not an unsigned Replay 1.20 fastfile", fastfilePath.c_str());
        return false;
    }

    IW8_DB_FFHeader header{};
    std::memcpy(&header, bytes.data(), (std::min)(bytes.size(), sizeof(header)));
    std::printf("file: %s\n", fastfilePath.c_str());
    std::printf("magic: %.8s\n", header.magic);
    std::printf("header version: %u\n", header.headerVersion);
    std::printf("xfile version: 0x%X\n", header.xfileVersion);
    std::printf("resident bytes: %u\n", header.residentPartSize);
    std::printf("decompressed bytes: %llu\n",
                static_cast<unsigned long long>(header.xfileHeader.size));
    std::printf("streams:");
    for (int index = 0; index < iw8ff::kNumStreams; ++index)
    {
        std::printf(" %llu", static_cast<unsigned long long>(header.xfileHeader.blockSize[index]));
    }
    std::printf("\n");
    return true;
}
} // namespace zt