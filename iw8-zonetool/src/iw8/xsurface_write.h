

#pragma once
#include <string>

namespace iw8 {
class ZoneWriter;
}
namespace iw3sr {
class ZoneSource;
}

namespace iw8xs {

// Read xsurface/<name>.xsurf_bin from `zs` and emit an IW8 XModelSurfs asset body into `zw` (the
// XAsset[] entry framing is done by the caller / ZoneWriter::build). Returns true on success; on a
// missing/corrupt blob it emits a minimal name-only XModelSurfs (load-safe) and returns false-ish via
// the log (still writes a valid struct so the zone stays parseable).
bool writeXModelSurfs(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const std::string& name);

} // namespace iw8xs
