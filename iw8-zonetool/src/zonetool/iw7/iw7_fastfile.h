#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw7
{
struct FastfileInfo
{
    bool signedFile{};
    uint32_t version{};
    uint32_t sharedStreamCount{};
    uint32_t imageStreamCount{};
    uint32_t blockSize{};
    uint8_t compressionType{};
    uint64_t uncompressedSize{};
    uint32_t scriptStringCount{};
    uint32_t assetCount{};
    std::vector<uint16_t> packageIndices;
};

bool inspectFastfile(const std::string &path, FastfileInfo &info, std::string &error);
std::vector<std::string> requiredPackages(const FastfileInfo &info);
} // namespace iw7
