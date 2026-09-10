#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace iw3
{
struct ImportOptions
{
    std::filesystem::path fastfile;
    std::string map;
    std::filesystem::path unlinker;
    std::vector<std::filesystem::path> searchPaths;
};

struct PreparedMap
{
    std::filesystem::path root;
    std::filesystem::path collision;
    std::filesystem::path scratch;

    PreparedMap() = default;
    PreparedMap(const PreparedMap &) = delete;
    PreparedMap &operator=(const PreparedMap &) = delete;
    PreparedMap(PreparedMap &&other) noexcept;
    PreparedMap &operator=(PreparedMap &&other) noexcept;
    ~PreparedMap();
};

PreparedMap PrepareFastfile(const ImportOptions &options);
} // namespace iw3
