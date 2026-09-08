

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace iw3maps {

// Locate the IW3 entityString blob in the inflated zone buffer. Searches for the worldspawn marker,
// then expands to the surrounding contiguous printable-ASCII region bounded by '{' ... final '}'.
//   zone      = the full inflated IW3 zone bytes.
//   outString = the extracted entityString (string-keyed IW3 form), NUL excluded.
// Returns true if a worldspawn entity block was found.
bool extractEntityString(const std::vector<uint8_t>& zone, std::string& outString);

} // namespace iw3maps
