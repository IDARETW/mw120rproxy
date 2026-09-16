#pragma once

#include "iw8_zone.h"

#include <string>

namespace iw8::replay_script
{
// Emits the small map-startup ScriptFile used by the stock IW8 compass setup.
// The body calls the stock common helper through its signed Replay token ids.
void emitCompassStartup(ZoneWriter &writer, const std::string &assetName,
                        const std::string &mapName);
} // namespace iw8::replay_script
