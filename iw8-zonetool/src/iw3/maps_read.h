// maps_read.h — Stage-A IW3 MAPS-family extraction helpers (offline; no game memory).
// Namespaced iw3maps:: to avoid clashes with other families.
//
// The foundation IW3 loader (iw3_zone.cpp) intentionally does NOT yet walk the full IW3 asset graph
// (that requires every type's field walk; SPEC §2b sanctions stopping at / scanning for the map asset).
// For the MAPS family the one piece of authored content we need is the entityString blob (worldspawn +
// spawn points). It is a large contiguous ASCII text region in the inflated zone, uniquely identifiable
// by "{ ... \"classname\" \"worldspawn\" ... }". extractEntityString() locates it directly from the
// flat buffer — robust against not having reproduced the full pointer-fixup graph.
//
// clipMap bounds / comworld are emitted valid-empty/default (the prototype uses null-havok collision +
// generous broadphase bounds = the proven map_zone.h shape), so Stage A records defaults for them. The
// dynentitylist is recorded empty (load-critical MapEnts emits 0 dynents; doc 10 §"Minimal").
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
