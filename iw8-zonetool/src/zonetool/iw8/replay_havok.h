#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace iw8::havok
{

struct BakeInput
{
    std::filesystem::path replayExecutable;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
};

std::vector<std::uint8_t> BakeCollision(const BakeInput &input);

} // namespace iw8::havok
