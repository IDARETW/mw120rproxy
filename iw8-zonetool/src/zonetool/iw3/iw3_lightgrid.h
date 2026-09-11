#pragma once

#include <filesystem>
#include <string>

namespace iw3
{
bool PrepareNativeLightGrid(const std::filesystem::path &sourceRoot,
                            const std::string &sourceAsset,
                            const std::filesystem::path &output);
}
