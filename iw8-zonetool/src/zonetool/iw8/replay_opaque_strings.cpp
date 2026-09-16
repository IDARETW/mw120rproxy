#include "replay_opaque_strings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace iw8
{
namespace
{
struct OpaqueStringEntry
{
    std::uint64_t hash;
    std::uint32_t id;
};

#include "replay_opaque_strings.inc"

std::uint64_t HashOpaqueString(const std::string_view value)
{
    constexpr std::uint64_t offsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = offsetBasis;
    for (auto character : value)
    {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
        hash ^= static_cast<unsigned char>(character);
        hash *= prime;
    }
    return hash;
}
} // namespace

bool FindReplayOpaqueString(const std::string_view value, std::uint32_t &id)
{
    const auto hash = HashOpaqueString(value);
    const auto found =
        std::lower_bound(kReplayOpaqueStrings.begin(), kReplayOpaqueStrings.end(), hash,
                         [](const OpaqueStringEntry &entry, const std::uint64_t wanted) {
                             return entry.hash < wanted;
                         });
    if (found == kReplayOpaqueStrings.end() || found->hash != hash)
        return false;
    id = found->id;
    return true;
}
} // namespace iw8
