#pragma once
#include <string>

namespace iw8
{
class ZoneWriter;
}

namespace iw8xs_dump
{

// Read <dumpDir>/XSurface/<name>.xse, convert IW5->IW8, and emit an IW8 XModelSurfs asset BODY into
// `zw` (the XAsset[] entry framing is done by ZoneWriter::build). Returns true on a clean geometry
// emit; false (after emitting a load-safe minimal name-only XModelSurfs) on any missing/corrupt
// input.
//   - name: the XModelSurfs asset name == the .xse stem ("zonetool_<model>_<lod>").
bool writeXModelSurfsFromDump(iw8::ZoneWriter &zw, const std::string &dumpDir,
                              const std::string &name);

} // namespace iw8xs_dump
