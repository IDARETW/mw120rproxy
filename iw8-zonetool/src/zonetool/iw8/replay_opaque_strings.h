#pragma once

#include <cstdint>
#include <string_view>

namespace iw8
{
bool FindReplayOpaqueString(std::string_view value, std::uint32_t &id);
}
