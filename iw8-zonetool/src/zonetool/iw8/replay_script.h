#pragma once

#include "iw8_zone.h"

#include <string>

namespace iw8::replay_script
{
// Emits the small map-startup ScriptFile used by the stock IW8 compass setup.
void emitCompassStartup(ZoneWriter &writer, const std::string &assetName,
                        const std::string &mapName, float northwestX, float northwestY,
                        float southeastX, float southeastY);
} // namespace iw8::replay_script
