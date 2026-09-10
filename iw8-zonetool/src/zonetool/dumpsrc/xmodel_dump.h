#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dumpsrc
{

// ===================================================================================================
// Stage A result — XModelDumpFull. One LOD's surface-link + the header counts + bone/material
// NAMES. Pointer/geometry blobs are NOT kept: IW8 references XModelSurfs/Material by name
// (cross-ref), and the geometry lives in the sibling .xse files (the xsurface family owns those).
// We keep only what the IW8 XModel(9) struct needs: counts, scalars, bounds(none in IW5 header —
// derived), per-LOD link names, and material names.
// ===================================================================================================
struct XModelLodDump
{
    float dist = 0.f;                   // IW5 XSurfaceLod.dist
    uint16_t numSurfacesInLod = 0;      // IW5 XSurfaceLod.numSurfacesInLod
    uint16_t surfIndex = 0;             // IW5 XSurfaceLod.surfIndex (running surf offset)
    std::array<uint32_t, 6> partBits{}; // IW5 XSurfaceLod.partBits[6] (24B)
    std::string surfsName;              // read_asset<ModelSurface> name "zonetool_<model>_<lod>"
                                        //   ("" if this LOD has no surface asset)
};

struct XModelDumpFull
{
    bool loaded = false; // true once the .xme6 parsed clean (cursor reached EOF)
    bool clean = false;  // true if the full walk consumed EXACTLY the whole file (no desync)
    std::string name;    // XModel asset name (== file stem)

    // header counts (from the IW5 32-bit XModel blob — offsets validated against real bytes)
    uint8_t numBones = 0;     // @+4
    uint8_t numRootBones = 0; // @+5
    uint8_t numSurfaces = 0;  // @+6  (== IW8 XModel.numsurfs; == sum of lod numSurfacesInLod)
    uint8_t lodRampType = 0;  // @+7
    float scale = 1.f;        // @+8
    uint8_t numLods = 0;      // @+241
    int8_t collLod = -1;      // @+242
    uint8_t flags = 0;        // @+243 (IW5 XModel.flags — char)
    int32_t numColSurfs = 0;  // @+248
    int32_t contents = 0;     // @+252 (IW5 XModel.contents)
    float radius = 0.f;       // @+260 (IW5 XModel.radius)
    float boundsMid[3] = {0, 0,
                          0}; // @+264 (IW5 Bounds.midPoint) — model-space, == IW8 Bounds.midPoint
    float boundsHalf[3] = {0, 0, 0}; // @+276 (IW5 Bounds.halfSize) — == IW8 Bounds.halfSize

    // bone names (script strings; [numBones]) — kept for completeness / boneName re-emit (offline =
    // 0 ids)
    std::vector<std::string> boneNames;

    // per-LOD surface links + scalars ([numLods] meaningful; up to 4 in IW5)
    std::array<XModelLodDump, 4> lods{};

    // material asset names ([numSurfaces]); OFFSET-deduped entries are resolved back to a real name
    // by the BinReader, so duplicates appear as repeated real names (never empty unless truly
    // null).
    std::vector<std::string> materials;

    // sub-asset links (may be empty)
    std::string physPresetName;
    std::string physCollmapName;

    // convenience: the lod0 XModelSurfs name (what the IW8 XModel primarily cross-references)
    const std::string &primarySurfs() const
    {
        for (const auto &l : lods)
            if (!l.surfsName.empty())
                return l.surfsName;
        static const std::string empty;
        return empty;
    }
};

// Read XModel/<name>.xme6 under <dumpDir> into `out`. Returns out.loaded. On a mid-stream desync
// the header counts + name are still returned (out.loaded stays true if the header+name read),
// out.clean is set false, and a warning is logged. Fully offline (file-in only).
bool readXModel(const std::string &dumpDir, const std::string &name, XModelDumpFull &out);

// Lower-level: parse an already-loaded .xme6 byte buffer. (readXModel = read file + this.)
bool parseXModel(const std::vector<uint8_t> &bytes, const std::string &nameHint,
                 XModelDumpFull &out);

} // namespace dumpsrc

// ===================================================================================================
// Stage B input — Iw8XModelRecord: the IW5->IW8 converted, writer-ready record. Mirrors exactly the
// fields the IW8 XModel(9)=0x2B0 struct + its trailing arrays need; every cross-reference is a NAME
// the writer turns into a tagged pointer (NULL for the load-safe converter, packed-offset once a
// zone-wide asset table exists). Owned here so reader/converter/writer share ONE definition.
// ===================================================================================================
namespace convert::xmodel
{

struct Iw8LodInfo
{
    float dist = 0.f;
    uint16_t numsurfs = 0;              // XModelLodInfo.numsurfs (= IW5 numSurfacesInLod)
    uint16_t surfIndex = 0;             // XModelLodInfo.surfIndex
    std::array<uint32_t, 8> partBits{}; // IW8 DObjPartBits 32B (IW5 24B zero-extended)
    uint8_t flags = 0;
    std::string surfsName; // XModelSurfs cross-ref name (lod link)
};

struct Iw8XModelRecord
{
    std::string name;

    uint16_t numsurfs = 0; // XModel.numsurfs (u16 in IW8)
    uint8_t numLods = 0;
    uint8_t collLod = 0;
    uint8_t numBones = 0;
    uint8_t numRootBones = 0;
    uint16_t numClientBones = 0;
    uint8_t shadowCutoffLod = 0;
    uint8_t lodRampType = 0; // not an IW8 field; kept for fidelity/debug (unused by writer)
    uint32_t flags = 0;
    int32_t contents = 0;
    float scale = 1.f;
    float radius = 0.f;
    float boundsMid[3] = {0, 0, 0};
    float boundsHalf[3] = {0, 0, 0};

    std::vector<std::string> boneNames; // [numBones] (offline: emitted as zeroed script-string ids)
    std::vector<Iw8LodInfo> lods;       // [numLods]  (XModel.lodInfo[6]; extras zeroed by writer)
    std::vector<std::string> materials; // [numsurfs] material cross-ref names (materialHandles)

    std::string physPresetName; // informational (PhysicsAsset cross-ref deferred)
    std::string physCollmapName;
};

// IW5 dump -> IW8 record. Pure field mapping (no I/O). Returns true on success.
bool toIw8(const dumpsrc::XModelDumpFull &in, Iw8XModelRecord &out);

} // namespace convert::xmodel

// ===================================================================================================
// Stage C — the IW8 writer entry points (defined in src/iw8/write_xmodel.cpp).
// ===================================================================================================
namespace iw8
{
class ZoneWriter;
}

namespace iw8
{

// Register the IW8 XModel(9) asset body for `rec` into the writer (zw.add(...) with the body
// callback). The body serializes the 0x2B0 struct + trailing arrays in load-read order with the raw
// tagged-pointer sentinels. Cross-refs (materialHandles, lod surfs) are NULL in the converter
// (load-safe; see notes in write_xmodel.cpp). Call before zw.build().
void writeXModel(ZoneWriter &zw, const convert::xmodel::Iw8XModelRecord &rec);

// Convenience: read <dumpDir>/XModel/<name>.xme6 -> convert -> register. Returns false if the .xme6
// is missing or unparseable; the caller logs and skips that asset.
bool addXModelFromDump(ZoneWriter &zw, const std::string &dumpDir, const std::string &name);

} // namespace iw8
