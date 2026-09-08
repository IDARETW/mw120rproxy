

#pragma once
#include <cstdint>

namespace xsurf_bin {

// magic (<=7 chars for ZoneSource::wrapBlob) + version. Bump VERSION on any layout change.
static constexpr char MAGIC[] = "XSURF";
static constexpr uint32_t VERSION = 1;

#pragma pack(push, 1)

// ---- file payload header (immediately after the 16-byte wrapBlob envelope) ----------------------
// Followed by: name[nameLen] (NUL-terminated, counted) then SurfHeader[surfCount], each surf's
// variable arrays appended in the field order documented on SurfHeader.
struct FileHeader {
    uint32_t surfCount;
    uint32_t nameLen; // length of the name string INCLUDING the trailing NUL
    uint32_t partBits[8];
    uint32_t flags;       // reserved (0)
    uint32_t reserved[3]; // reserved (0) — keeps the header at a tidy 0x38 bytes
};

struct SurfHeader {
    uint16_t flags;             // IW3 XSurface flags (tileMode/deformed folded in below instead)
    uint8_t tileMode;           // IW3 XSurface.tileMode
    uint8_t deformed;           // IW3 XSurface.deformed (0/1)
    uint16_t vertCount;         // render vertices
    uint16_t triCount;          // triangles (indices = triCount*3)
    uint16_t baseTriIndex;      // IW3 XSurface.baseTriIndex
    uint16_t baseVertIndex;     // IW3 XSurface.baseVertIndex
    uint8_t rigidVertListCount; // XRigidVertList entries
    uint8_t _pad0;
    uint32_t blendVertWords;     // number of uint16_t words in the blendVerts stream (0 if none)
    uint16_t vertBlendCounts[4]; // IW3 XSurfaceVertexInfo.vertCount[4] (blend tiers)
    uint32_t partBits[8];        // IW3 XSurface.partBits (4 ints widened to 8, hi=0)
};

// ---- one unpacked render vertex (engine-neutral) ------------------------------------------------
struct GfxVertex {
    float pos[3];       // position (model space)
    float normal[3];    // unit normal (unpacked from IW3 PackedUnitVec)
    float tangent[3];   // unit tangent (unpacked from IW3 PackedUnitVec)
    float binormalSign; // IW3 binormalSign (handedness)
    float uv[2];        // texcoord (unpacked from IW3 PackedTexCoords half-floats)
    uint8_t color[4];   // RGBA vertex color
};

// ---- a rigid (single-bone) vertex run -----------------------------------------------------------
struct RigidVertList {
    uint16_t boneOffset; // IW3 boneOffset (bone-index*sizeof(DObjAnimMat) style; raw IW3 value)
    uint16_t vertCount;
    uint16_t triOffset;
    uint16_t triCount;
    // (IW3's XRigidVertList.collisionTree is intentionally dropped — not load-required in IW8.)
};

#pragma pack(pop)

static_assert(sizeof(FileHeader) == 0x38, "xsurf_bin FileHeader"); // 4+4+32+4+12
static_assert(sizeof(SurfHeader) == 0x3A, "xsurf_bin SurfHeader"); // 2+1+1+2+2+2+2+1+1+4+8+32
static_assert(sizeof(GfxVertex) == 0x34, "xsurf_bin GfxVertex");   // 12+12+12+4+8+4
static_assert(sizeof(RigidVertList) == 0x08, "xsurf_bin RigidVertList");

} // namespace xsurf_bin
