// conv_xsurface.h — IW5(.xse)->IW8 XModelSurfs field mapping for the NEW dump path.
// =================================================================================================
// Owns the IW5 -> IW8 conversion for the xsurface family: decode the IW5 packed vertices/indices/runs
// (dumpsrc::XseFile, from read_xsurface.cpp) and produce an IW8-READY intermediate (Iw8Surfs) that the
// serializer (iw8/write_xsurface.cpp) writes straight into the ZoneWriter. ALL repacking math lives
// here; the reader keeps IW5 packed forms verbatim and the writer does no math.
//
// IW8 GEOMETRY MODEL (reference/iw8_focus_structs.h, dev.i64-pinned):
//   XModelSurfs{ name, surfs[numsurfs], xpakEntry(scalar,0), shared, numsurfs, ugbState, partBits[32] }
//   XSurface[i]{ vertCount, triCount, ..., sharedVertDataOffset, sharedIndexDataOffset, ..., shared,
//                surfBounds, partBits[32] }  -- NO inline geometry; only offsets into shared.data.
//   XSurfaceShared{ data, dataSize, flags }  -- ONE packed blob holding every surface's verts+indices.
// We build a single shared blob = concat over surfaces of [ GfxPackedVertex verts ][ u16 indices ],
// 16-byte aligned per region, and record each surface's byte offsets into it.
//
// PACKING (reference/iw8_focus_structs.h GfxPackedVertex 0x14): xyz -> u64 PackedPosition (21b/axis,
// quantized into the per-surface bounds), texCoord -> u32 two half-floats, tangentFrame -> u32 dec3n of
// the normal (the engine derives a usable tangent frame; full quaternion-basis fidelity is render-
// quality, not a load gate — SPEC §1). selfVisibility = 0. The packers are reused from xsurf_conv::
// (convert/xsurface_convert.h) — the same math the legacy .xsurf_bin path uses, so both paths agree.
//
// LOAD-SAFETY: every produced surface's per-surface pointer fields are null EXCEPT shared (which the
// writer points at the one blob). rigidVertLists/blendVerts/subdiv/childBounds are all dropped (set
// null + count 0) — they are NOT load-required for the prototype's geometry (SPEC §1). Documented in
// the writer's return notes.
// =================================================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc { struct XseFile; }

namespace conv_xsurf {

// ---- one IW8-ready surface (field values; pointer sentinels are stamped by the writer) -----------
struct Iw8SurfaceCvt {
    uint16_t vertCount = 0;            // -> XSurface.vertCount
    uint16_t triCount  = 0;            // -> XSurface.triCount (index count = triCount*3)
    uint32_t sharedVertDataOffset  = 0;// byte offset of this surf's GfxPackedVertex[] in the shared blob
    uint32_t sharedIndexDataOffset = 0;// byte offset of this surf's u16 indices[] in the shared blob
    float    boundsMid[3]  = {0,0,0};  // -> XSurface.surfBounds.midPoint (also the position-pack center)
    float    boundsHalf[3] = {1,1,1};  // -> XSurface.surfBounds.halfSize (also the position-pack extent)
    uint32_t partBits[8]   = {0,0,0,0,0,0,0,0}; // -> XSurface.partBits[32] (IW5 6 ints widened, hi=0)
};

// ---- the whole converted XModelSurfs (one .xse / one LOD) ----------------------------------------
struct Iw8Surfs {
    bool        ok = false;            // false => caller emits a load-safe minimal XModelSurfs
    std::string name;                  // == XseFile.name ("zonetool_<model>_<lod>")
    uint32_t    partBits[8] = {0,0,0,0,0,0,0,0}; // -> XModelSurfs.partBits[32] (ModelSurface 6 ints widened)
    std::vector<Iw8SurfaceCvt> surfaces;
    std::vector<uint8_t>       sharedBlob;  // the packed XSurfaceShared.data (verts+indices, all surfs)
    // diagnostics (returned to the caller's log)
    uint32_t totalVerts = 0;
    uint32_t totalTris  = 0;
    uint32_t droppedRigidRuns = 0;     // rigidVertLists not carried (informational)
    uint32_t droppedBlendVerts = 0;    // vertsBlend words not carried (informational)
};

// Convert a parsed .xse into the IW8-ready intermediate. Returns Iw8Surfs.ok=true when the input had
// >=1 surface and converted cleanly; ok=false (with an empty result) when the .xse had 0 surfaces or
// failed to load. NEVER throws. Pure CPU; no IO.
Iw8Surfs convert(const dumpsrc::XseFile& xse);

} // namespace conv_xsurf
