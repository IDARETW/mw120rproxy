// xsurface_read.h — Stage-A IW3 xsurface reader entry points (xsurface family).
// =================================================================================================
// The registry hook iw3_dump_xsurface(LoadCtx&, ZoneSource&) (convert/registry.h) handles the case
// where the loader dispatches a standalone XModelSurfs-style asset (cursor at an XSurface-array head).
// In CoD4 the XSurfaces actually live INLINE inside XModel (XModel.surfs[numsurfs]); the xmodel family
// reaches them and calls dumpXSurfacesForModel() below with the parent's count + name so the geometry
// lands at xsurface/<name>.xsurf_bin and the manifest gets an "xmodelsurfs" row.
//
// Both entry points read the IW3 XSurface array + its pointer graph (verts0/triIndices/vertList/
// vertsBlend) from the flat inflated zone via LoadCtx::resolve, unpack to engine-neutral floats, and
// write the self-describing .xsurf_bin (convert/xsurface_binfmt.h).
//
// Owned by the xsurface family; namespaced iw3xs:: to avoid clashes.
// =================================================================================================
#pragma once
#include <cstdint>
#include <string>

namespace iw3 {
class LoadCtx;
}
namespace iw3sr {
class ZoneSource;
}

namespace iw3xs {

// Read `numsurfs` IW3 XSurface structs whose array begins at absolute zone offset `surfsOff`, plus
// each surface's geometry (verts/tris/weights/vertlists), and dump them to xsurface/<name>.xsurf_bin
// (adding the "xmodelsurfs" manifest row). `lc` provides the flat zone + resolve(). Returns true on
// success (false on a hard read error). Used by the xmodel reader (the IW3-native path).
bool dumpXSurfacesForModel(iw3::LoadCtx& lc,
                           iw3sr::ZoneSource& zs,
                           uint32_t surfsOff,
                           uint32_t numsurfs,
                           const std::string& name);

} // namespace iw3xs
