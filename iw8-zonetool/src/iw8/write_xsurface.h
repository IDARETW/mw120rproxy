// write_xsurface.h — Stage-B IW8 xsurface writer for the NEW dump path.
// =================================================================================================
// Serializes an IW8 (MW2019 1.24) XModelSurfs(8)=0x60 + XSurface(0xC0)[] + XSurfaceShared graph into
// the ZoneWriter, sourced DIRECTLY from a ZoneTool .xse dump (NOT the legacy .xsurf_bin path). The
// chain is: dumpsrc::loadXseFile -> conv_xsurf::convert -> this writer.
//
// Distinct from the legacy iw8xs::writeXModelSurfs (xsurface_write.h), which reads .xsurf_bin from an
// iw3sr::ZoneSource. This entry takes the dump directory + the surface asset name, so the dump CLI
// (cmd_fromdump) can drive it with no zone-source round-trip. Namespaced iw8xs_dump:: to avoid clashes.
//
// FLAG-GATED (Tier2): only invoked when main runs with --assets, so it can NOT break the Tier1 map
// zone. On a missing/corrupt .xse it emits a minimal, load-safe name-only XModelSurfs and returns false.
// =================================================================================================
#pragma once
#include <string>

namespace iw8 { class ZoneWriter; }

namespace iw8xs_dump {

// Read <dumpDir>/XSurface/<name>.xse, convert IW5->IW8, and emit an IW8 XModelSurfs asset BODY into
// `zw` (the XAsset[] entry framing is done by ZoneWriter::build). Returns true on a clean geometry
// emit; false (after emitting a load-safe minimal name-only XModelSurfs) on any missing/corrupt input.
//   - name: the XModelSurfs asset name == the .xse stem ("zonetool_<model>_<lod>").
bool writeXModelSurfsFromDump(iw8::ZoneWriter& zw, const std::string& dumpDir, const std::string& name);

} // namespace iw8xs_dump
