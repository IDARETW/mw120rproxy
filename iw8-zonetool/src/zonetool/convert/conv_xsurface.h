#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc
{
struct XseFile;
}

namespace conv_xsurf
{

struct Iw8RigidVertList
{
    uint16_t boneIndex{}, vertCount{}, triOffset{}, triCount{};
};
static_assert(sizeof(Iw8RigidVertList) == 8);

// ---- one IW8-ready surface (field values; pointer sentinels are stamped by the writer)
// -----------
struct Iw8SurfaceCvt
{
    uint16_t flags = 0;
    uint16_t vertCount = 0; // -> XSurface.vertCount
    uint16_t triCount = 0;  // -> XSurface.triCount (index count = triCount*3)
    uint32_t sharedVertDataOffset =
        0; // byte offset of this surf's GfxPackedVertex[] in the shared blob
    uint32_t sharedIndexDataOffset =
        0; // byte offset of this surf's u16 indices[] in the shared blob
    uint32_t sharedTriClusterDataOffset =
        0; // byte offset of GfxPackedTriCluster[ceil(triCount / 64)]
    uint32_t sharedColorDataOffset = UINT32_MAX;
    std::vector<Iw8RigidVertList> rigidVertLists;
    uint16_t blendVertCounts[8]{};
    std::vector<uint16_t> blendVerts;
    float boundsMid[3] = {0, 0,
                          0}; // -> XSurface.surfBounds.midPoint (also the position-pack center)
    float boundsHalf[3] = {1, 1,
                           1}; // -> XSurface.surfBounds.halfSize (also the position-pack extent)
    uint32_t partBits[8] = {0, 0, 0, 0,
                            0, 0, 0, 0}; // -> XSurface.partBits[32] (IW5 6 ints widened, hi=0)
};

// ---- the whole converted XModelSurfs (one .xse / one LOD)
// ----------------------------------------
struct Iw8Surfs
{
    bool ok = false;
    std::string name; // == XseFile.name ("zonetool_<model>_<lod>")
    uint32_t partBits[8] = {0, 0, 0, 0, 0,
                            0, 0, 0}; // -> XModelSurfs.partBits[32] (ModelSurface 6 ints widened)
    std::vector<Iw8SurfaceCvt> surfaces;
    std::vector<uint8_t> sharedBlob; // the packed XSurfaceShared.data (verts+indices, all surfs)
    // diagnostics (returned to the caller's log)
    uint32_t totalVerts = 0;
    uint32_t totalTris = 0;
    uint32_t totalTriClusters = 0;
    uint32_t totalRigidRuns = 0;
    uint32_t totalBlendWords = 0;
};

// Convert a parsed .xse into the IW8-ready intermediate. Returns Iw8Surfs.ok=true when the input
// had
// >=1 surface and converted cleanly; ok=false (with an empty result) when the .xse had 0 surfaces
// or failed to load. Rejects counts and indices that cannot be represented by Replay. Pure CPU; no
// IO.
Iw8Surfs convert(const dumpsrc::XseFile &xse);

// UGB identity must include the final vertex attributes, including importer metadata.
uint32_t surfaceHash(const Iw8Surfs &surfs, const Iw8SurfaceCvt &surface);

} // namespace conv_xsurf
