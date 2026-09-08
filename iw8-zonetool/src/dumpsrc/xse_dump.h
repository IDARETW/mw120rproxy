

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc {

struct XseVertex {
    float xyz[3] = {0, 0, 0};
    float binormalSign = 0.f;
    uint32_t color = 0;    // GfxColor.packed (RGBA bytes)
    uint32_t texCoord = 0; // PackedTexCoords (two half-floats: lo=u, hi=v)
    uint32_t normal = 0;   // PackedUnitVec (4 signed bytes)
    uint32_t tangent = 0;  // PackedUnitVec
};

struct XseRigidVertList {
    uint16_t boneOffset = 0;
    uint16_t vertCount = 0;
    uint16_t triOffset = 0;
    uint16_t triCount = 0;
};

// ---- one XSurface entry inside the .xse (a ModelSurface holds xSurficiesCount of these) ----------
struct XseSurface {
    // scalar header fields (read as 4 dump_int's per IW5_DUMP_FORMAT §5)
    int32_t tileMode = 0;
    int32_t deformed = 0; // 0/1 (1 = skinned; vertexInfo/vertsBlend populated)
    int32_t baseTriIndex = 0;
    int32_t baseVertIndex = 0;
    int32_t partBits[6] = {0, 0, 0, 0, 0, 0};

    int16_t vertBlendCounts[4] = {0, 0, 0, 0};
    std::vector<uint16_t> vertsBlend; // raw IW5 blend-weight index stream (skinning), kept verbatim

    uint32_t vertCount = 0;           // == verticies.size()
    std::vector<XseVertex> verticies; // [vertCount]

    uint32_t triCount = 0;            // tris; index count = triCount*3
    std::vector<uint16_t> triIndices; // [triCount*3] (each IW5 Face = u16 v1,v2,v3)

    uint32_t vertListCount = 0;                   // == rigidVertLists.size()
    std::vector<XseRigidVertList> rigidVertLists; // [vertListCount]
};

// ---- the whole .xse file (one ModelSurface = one LOD's surfaces) ---------------------------------
struct XseFile {
    bool loaded = false; // true only after a clean, fully-consumed parse
    std::string name;    // ModelSurface.name == "zonetool_<model>_<lod>"
    int32_t modelSurfPartBits[6] = {0, 0, 0, 0,
                                    0, 0}; // ModelSurface.partBits[6] (the XModelSurfs-level bits)
    std::vector<XseSurface> surfaces;      // [xSurficiesCount]
    std::string parseError;                // set when loaded==false (for the caller's log)
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
