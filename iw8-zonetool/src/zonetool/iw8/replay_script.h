#pragma once

#include "iw8_zone.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iw8::replay_script
{
// Encode bytes as a zlib stream using stored DEFLATE blocks. Replay accepts
// this representation for compressed ScriptFile and RawFile payloads.
std::vector<uint8_t> zlibStored(const std::vector<uint8_t> &input);

// Emits the small map-startup ScriptFile used by the stock IW8 compass setup.
void emitCompassStartup(ZoneWriter &writer, const std::string &assetName,
                        const std::string &mapName, float northwestX, float northwestY,
                        float southeastX, float southeastY);
} // namespace iw8::replay_script
