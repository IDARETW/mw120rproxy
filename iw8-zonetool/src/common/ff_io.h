#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zt
{
struct Iw8WriteParams
{
    uint64_t blockSize[11]{};
    uint64_t totalDecompressed{};
    uint64_t calcSize{};
    uint8_t transientFileType{};
};

bool iw8_write(const std::string &outputPath, const std::vector<uint8_t> &zoneBody,
               const Iw8WriteParams &params);
bool inspect_ff(const std::string &fastfilePath);
} // namespace zt