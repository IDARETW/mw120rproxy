#pragma once

#include "iw8_zone.h"

#include <string>

namespace iw8::impact
{

void Register(ZoneWriter &writer, const std::string &mapName,
              const std::string &smallGlassEffect = {});

} // namespace iw8::impact
