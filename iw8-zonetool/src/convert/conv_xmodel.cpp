// conv_xmodel.cpp — IW5 dump -> IW8 field mapping for the XModel(9) family (Tier2).
// Pure data transform (no I/O). Source = dumpsrc::XModelDumpFull (the IW5 32-bit dump, NAMES + counts);
// target = convert::xmodel::Iw8XModelRecord (writer-ready, the iw8_focus::XModel(0x2B0) fields).
//
// MAPPING NOTES (IW5 XModel -> IW8 XModel, per reference/iw8_asset_structs_pinned.h):
//   IW5.numSurfaces (u8)        -> IW8.numsurfs (u16)             [direct]
//   IW5.numBones/numRootBones   -> IW8.numBones/numRootBones      [direct]
//   IW5.numLods (<=4)           -> IW8.numLods                    [direct; IW8 has lodInfo[6], extras=0]
//   IW5.collLod                 -> IW8.collLod                    [direct]
//   IW5.flags (char)            -> IW8.flags (u32)                [zero-extended]
//   IW5.contents                -> IW8.contents                  [direct]
//   IW5.scale/radius            -> IW8.scale/radius               [direct]
//   IW5.bounds{mid,half}        -> IW8.bounds{midPoint,halfSize}  [direct copy; same layout]
//   IW5.boneNames[]             -> IW8.boneNames[]                [names kept; writer emits 0 ids offline]
//   per-LOD: IW5.numSurfacesInLod-> IW8.lodInfo.numsurfs; surfIndex->surfIndex; dist->dist;
//            partBits[6](24B)   -> IW8.lodInfo.partBits[8](32B)   [zero-extend the high 8 bytes]
//            ModelSurface name  -> IW8.lodInfo cross-ref (XModelSurfs link name)
//   IW5.materials[]             -> IW8.materialHandles[] cross-ref names
//   IW5.numClientBones          : not in the IW5 dump -> 0 (IW8 default; cloth/client bones unused here)
//   IW5.shadowCutoffLod         : not in the IW5 dump -> 0 (load-safe default)
// Fields with no IW5 source (numAimAssistBones, mdaoVolumeCount, numClothAssets, physicsUseCategory,
// proceduralBones, etc.) stay 0/null in the writer (load-safe — see write_xmodel.cpp).
#include "../dumpsrc/xmodel_dump.h"
#include "../common/log.h"
#include <cstring>

namespace convert::xmodel {

bool toIw8(const dumpsrc::XModelDumpFull& in, Iw8XModelRecord& out) {
    if (!in.loaded) {
        zt::warn("conv_xmodel: source '%s' not loaded", in.name.c_str());
        return false;
    }
    out = Iw8XModelRecord{};
    out.name           = in.name;
    out.numsurfs       = in.numSurfaces;
    out.numLods        = in.numLods;
    out.collLod        = (uint8_t)in.collLod;     // IW8 collLod is u8; IW5 -1 -> 0xFF (no coll lod)
    out.numBones       = in.numBones;
    out.numRootBones   = in.numRootBones;
    out.numClientBones = 0;
    out.shadowCutoffLod= 0;
    out.lodRampType    = in.lodRampType;
    out.flags          = (uint32_t)in.flags;      // zero-extend char -> u32
    out.contents       = in.contents;
    out.scale          = (in.scale != 0.f) ? in.scale : 1.f;
    out.radius         = in.radius;
    for (int k = 0; k < 3; ++k) {
        out.boundsMid[k]  = in.boundsMid[k];
        out.boundsHalf[k] = in.boundsHalf[k];
    }

    out.boneNames = in.boneNames;                 // [numBones]

    // per-LOD info
    const int nl = (in.numLods <= 4) ? in.numLods : 4;
    out.lods.reserve(nl);
    for (int i = 0; i < nl; ++i) {
        const dumpsrc::XModelLodDump& L = in.lods[i];
        Iw8LodInfo o;
        o.dist      = L.dist;
        o.numsurfs  = L.numSurfacesInLod;
        o.surfIndex = L.surfIndex;
        o.flags     = 0;                          // IW8 XModelLodInfoFlags — none derivable from IW5
        // partBits: IW5 6×u32 (24B) -> IW8 8×u32 (32B), zero-extend the trailing 8 bytes.
        for (int k = 0; k < 6; ++k) o.partBits[k] = L.partBits[k];
        o.partBits[6] = 0; o.partBits[7] = 0;
        o.surfsName = L.surfsName;                // XModelSurfs link (== the .xse asset name)
        out.lods.push_back(o);
    }

    // materials (cross-ref names) — [numsurfs]. Empty names (null materials) are kept as "" so the
    // handle slot count stays == numsurfs; the writer emits a NULL Material* for those.
    out.materials = in.materials;
    if ((int)out.materials.size() != (int)out.numsurfs)
        out.materials.resize(out.numsurfs);       // clamp to declared count (writer-side safety)

    out.physPresetName  = in.physPresetName;
    out.physCollmapName = in.physCollmapName;
    return true;
}

} // namespace convert::xmodel
