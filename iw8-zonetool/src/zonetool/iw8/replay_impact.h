#pragma once

#include "iw8_zone.h"

#include <cstddef>
#include <string>
#include <vector>

namespace iw8::impact
{

struct EffectOverride
{
    std::size_t impactType{};
    std::size_t direction{};
    bool flesh{};
    std::size_t index{};
    // An empty effect deliberately clears the corresponding source slot.
    std::string effect;
};

using EffectOverrides = std::vector<EffectOverride>;

void Register(ZoneWriter &writer, const std::string &mapName,
              const std::string &smallGlassEffect = {},
              const EffectOverrides &overrides = {});

} // namespace iw8::impact
