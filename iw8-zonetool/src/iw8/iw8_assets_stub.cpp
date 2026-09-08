// iw8_assets_stub.cpp — no-op STUB definitions of the Stage-B IW8 writers (convert/registry.h).
// Each logs "TODO <fam>" and emits NOTHING into the zone (the asset is framed by the ZoneWriter's
// XAsset[] entry but its body is a no-op for now). The `maps` family delegates to the proven
// map_zone.h emitter when invoked via main's srv path; the standalone stub here stays a no-op so the
// generic dispatch links. Later per-asset agents REPLACE these.
#include "../convert/registry.h"
#include "../common/log.h"
#include "iw8_zone.h"
#include "../zonesrc/zone_source.h"

namespace convert {

// All five families (image, material, xmodel, xsurface, maps) are now IMPLEMENTED in their own
// iw8/asset_<fam>.cpp translation units. No stubs remain — this file is intentionally empty.

} // namespace convert
