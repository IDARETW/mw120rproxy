#pragma once
#include <string>

namespace iw8
{
class ZoneWriter;
}

namespace conv_xsurf
{
struct Iw8Surfs;
}

namespace iw8xs_dump
{

// Read <dumpDir>/XSurface/<name>.xse, convert IW5->IW8, and emit an IW8 XModelSurfs asset BODY into
// `zw` (the XAsset[] entry framing is done by ZoneWriter::build). Returns true on a clean geometry
// emit. Missing or corrupt input is rejected instead of emitting empty geometry.
//   - name: the XModelSurfs asset name == the .xse stem ("zonetool_<model>_<lod>").
bool writeXModelSurfsFromDump(iw8::ZoneWriter &zw, const std::string &dumpDir,
                              const std::string &name);
void writeXModelSurfs(iw8::ZoneWriter &zw, const std::string &name,
                      const conv_xsurf::Iw8Surfs &surfaces);

} // namespace iw8xs_dump
