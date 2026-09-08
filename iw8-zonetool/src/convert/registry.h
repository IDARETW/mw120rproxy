// registry.h — the asset registry: extern declarations of the per-family IW3-dump + IW8-write entry
// points. Per-asset modules (src/iw3/<fam>.cpp, src/iw8/<fam>.cpp) DEFINE these; the skeleton provides
// no-op stubs in src/convert/registry.cpp + the per-side stub .cpp so everything links.
//
// FOCUS families (SPEC §1): material, image, xmodel, xsurface, maps (map_ents + clipmap/col_map +
// comworld/com_map + dynentitylist).
//
// Signatures (per task spec):
//   iw3: void iw3_dump_<fam>(iw3::LoadCtx&, iw3sr::ZoneSource&);   // read IW3 struct -> zone-source
//   iw8: void iw8_write_<fam>(iw8::ZoneWriter&, iw3sr::ZoneSource&, const char* name); // build IW8
#pragma once

namespace iw3 { class LoadCtx; }
namespace iw8 { class ZoneWriter; }
namespace iw3sr { class ZoneSource; }

namespace convert {

// ---- Stage A: IW3 -> zone-source dumpers ---------------------------------------------------------
void iw3_dump_material (iw3::LoadCtx& lc, iw3sr::ZoneSource& zs);
void iw3_dump_image    (iw3::LoadCtx& lc, iw3sr::ZoneSource& zs);
void iw3_dump_xmodel   (iw3::LoadCtx& lc, iw3sr::ZoneSource& zs);
void iw3_dump_xsurface (iw3::LoadCtx& lc, iw3sr::ZoneSource& zs);
void iw3_dump_maps     (iw3::LoadCtx& lc, iw3sr::ZoneSource& zs);  // map_ents + clipmap + comworld + dynent

// ---- Stage B: zone-source -> IW8 zone writers ----------------------------------------------------
void iw8_write_material (iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name);
void iw8_write_image    (iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name);
void iw8_write_xmodel   (iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name);
void iw8_write_xsurface (iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name);
void iw8_write_maps     (iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name);

// ---- registry dispatch (Stage B) -----------------------------------------------------------------
// Dispatch a manifest entry's IW8 type-name string to the right iw8_write_<fam>. Returns false if the
// type is not handled (the caller logs + skips). Used by main.cpp 'build'.
bool dispatchIw8Write(const char* iw8TypeName, iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs,
                      const char* name);

} // namespace convert
