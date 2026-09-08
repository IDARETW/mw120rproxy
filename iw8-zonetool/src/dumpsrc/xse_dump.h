// xse_dump.h — parsed-in-memory view of a ZoneTool (IW5) XSurface dump file (.xse).
// =================================================================================================
// THE NEW DUMP->IW8 XSURFACE PATH (Tier2 family: xsurface -> IW8 XModelSurfs(8)=0x60 + XSurface(0xC0)).
// This header is the contract between the three new files:
//   src/dumpsrc/read_xsurface.cpp  parses XSurface\<name>.xse  ->  XseFile (this struct)
//   src/convert/conv_xsurface.cpp  maps XseFile (IW5 packed)   ->  IW8Surfs (IW8-ready, repacked)
//   src/iw8/write_xsurface.cpp     serializes IW8Surfs         ->  the ZoneWriter (XModelSurfs body)
//
// FORMAT AUTHORITY for the .xse byte stream = zonetool-develop/src/IW5/Assets/XSurface.cpp
// (IXSurface::parse / ::dump — the exact BinaryDumper read order) + reference/IW5_DUMP_FORMAT.md §5.
// VERIFIED against the real dump bytes (C:\Games\CoD4\dump\mp_test\XSurface\*.xse): the read order,
// tag bytes, array counts, and the vertsBlend word-count formula (vc0 + vc1*3 + vc2*5 + vc3*7) all
// reproduce a complete consume of zonetool_body_mp_arab_regular_assault_3.xse (0 bytes left over).
//
// The .xse carries IW5 32-bit packed geometry: GfxPackedVertex is float xyz + binormalSign + packed
// color/texcoord/normal/tangent (sizeof 32). We keep the IW5 packed forms verbatim in XseVertex so the
// converter (conv_xsurface) owns ALL repacking math — this header pulls in NO game headers and no
// packing logic. All counts are the on-disk values; arrays are exactly as long as their counts say.
// =================================================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc {

// ---- one IW5 packed render vertex (the .xse GfxPackedVertex, 32 B on disk, kept verbatim) --------
// Layout (IW5 Structs.hpp:733): float xyz[3]; float binormalSign; u32 color; u32 texCoord;
// u32 normal; u32 tangent. We keep the packed u32 fields raw — the IW5->IW8 converter decodes them.
struct XseVertex {
    float    xyz[3]      = {0,0,0};
    float    binormalSign = 0.f;
    uint32_t color        = 0;   // GfxColor.packed (RGBA bytes)
    uint32_t texCoord     = 0;   // PackedTexCoords (two half-floats: lo=u, hi=v)
    uint32_t normal       = 0;   // PackedUnitVec (4 signed bytes)
    uint32_t tangent      = 0;   // PackedUnitVec
};

// ---- one rigid (single-bone) vertex run (the .xse XRigidVertList, first 8 B; collisionTree dropped) -
// IW5 XRigidVertList (Structs.hpp:771) = u16 boneOffset, vertCount, triOffset, triCount, then a
// XSurfaceCollisionTree* (4 B in the 32-bit dump). We carry only the 4 u16 run fields (IW8's
// XRigidVertList is exactly these 4 u16, 0x08). The collisionTree (render-collision accel) is NOT
// load-required for the IW8 prototype and is intentionally not carried (documented in the return).
struct XseRigidVertList {
    uint16_t boneOffset = 0;
    uint16_t vertCount  = 0;
    uint16_t triOffset  = 0;
    uint16_t triCount   = 0;
};

// ---- one XSurface entry inside the .xse (a ModelSurface holds xSurficiesCount of these) ----------
struct XseSurface {
    // scalar header fields (read as 4 dump_int's per IW5_DUMP_FORMAT §5)
    int32_t  tileMode      = 0;
    int32_t  deformed      = 0;   // 0/1 (1 = skinned; vertexInfo/vertsBlend populated)
    int32_t  baseTriIndex  = 0;
    int32_t  baseVertIndex = 0;
    int32_t  partBits[6]   = {0,0,0,0,0,0};

    // XSurfaceVertexInfo.vertCount[4] (blend tiers). vertsBlend word count =
    //   vc[0] + vc[1]*3 + vc[2]*5 + vc[3]*7   (verified vs real bytes).
    int16_t  vertBlendCounts[4] = {0,0,0,0};
    std::vector<uint16_t> vertsBlend;   // raw IW5 blend-weight index stream (skinning), kept verbatim

    uint32_t              vertCount = 0; // == verticies.size()
    std::vector<XseVertex> verticies;    // [vertCount]

    uint32_t              triCount = 0;  // tris; index count = triCount*3
    std::vector<uint16_t> triIndices;    // [triCount*3] (each IW5 Face = u16 v1,v2,v3)

    uint32_t                       vertListCount = 0; // == rigidVertLists.size()
    std::vector<XseRigidVertList>  rigidVertLists;    // [vertListCount]
};

// ---- the whole .xse file (one ModelSurface = one LOD's surfaces) ---------------------------------
struct XseFile {
    bool        loaded = false;          // true only after a clean, fully-consumed parse
    std::string name;                    // ModelSurface.name == "zonetool_<model>_<lod>"
    int32_t     modelSurfPartBits[6] = {0,0,0,0,0,0}; // ModelSurface.partBits[6] (the XModelSurfs-level bits)
    std::vector<XseSurface> surfaces;    // [xSurficiesCount]
    std::string parseError;              // set when loaded==false (for the caller's log)
};

// Parse XSurface\<surfaceName>.xse from `xseFileBytes` (already read off disk). Returns an XseFile with
// loaded=true on a clean, fully-consumed parse; on any malformed/short stream it returns loaded=false
// with parseError set and the partial data discarded. NEVER throws.
//   - surfaceName: the asset name ("zonetool_<model>_<lod>"), used only for error messages; the real
//     name is read from the file's STRING primitive (and cross-checked, warn-only on mismatch).
XseFile parseXse(const std::vector<uint8_t>& xseFileBytes, const std::string& surfaceName);

// Convenience: read <dumpDir>/XSurface/<surfaceName>.xse off disk and parseXse() it. Returns
// loaded=false (with parseError) if the file is missing/unreadable. This is the entry the IW8 writer
// (write_xsurface.cpp) and the dump CLI use — it does NOT touch DumpSource (whose public loadXSurface
// stub stays as-is) so there is no symbol clash. Fully offline.
XseFile loadXseFile(const std::string& dumpDir, const std::string& surfaceName);

} // namespace dumpsrc
