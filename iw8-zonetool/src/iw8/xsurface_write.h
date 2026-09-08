// xsurface_write.h — Stage-B IW8 xsurface writer entry (xsurface family).
// =================================================================================================
// Rebuilds an IW8 (MW2019 1.24) XModelSurfs(8)=0x60 + XSurface(0xC0)[] + XSurfaceShared graph from the
// engine-neutral xsurface/<name>.xsurf_bin (convert/xsurface_binfmt.h) and serializes it into the
// ZoneWriter in load-read order with the IW8 tagged-pointer sentinels (-2 follows / -3 insert / 0
// null). Field map = src/iw8/iw8_focus_structs.h (dev.i64-pinned, size-asserted vs g_assetSizes).
//
// The registry hook iw8_write_xsurface(ZoneWriter&, ZoneSource&, name) (convert/registry.h) forwards
// here. Owned by the xsurface family; namespaced iw8xs:: to avoid clashes.
// =================================================================================================
#pragma once
#include <string>

namespace iw8 { class ZoneWriter; }
namespace iw3sr { class ZoneSource; }

namespace iw8xs {

// Read xsurface/<name>.xsurf_bin from `zs` and emit an IW8 XModelSurfs asset body into `zw` (the
// XAsset[] entry framing is done by the caller / ZoneWriter::build). Returns true on success; on a
// missing/corrupt blob it emits a minimal name-only XModelSurfs (load-safe) and returns false-ish via
// the log (still writes a valid struct so the zone stays parseable).
bool writeXModelSurfs(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const std::string& name);

} // namespace iw8xs
