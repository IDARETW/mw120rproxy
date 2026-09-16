#include "../../common/log.h"
#include "../dumpsrc/xmodel_dump.h"
#include <cstring>

namespace convert::xmodel
{

bool toIw8(const dumpsrc::XModelDumpFull &in, Iw8XModelRecord &out)
{
    if (!in.loaded || !in.clean || in.numLods > in.lods.size() || in.numRootBones > in.numBones ||
        in.boneNames.size() != in.numBones || in.materials.size() != in.numSurfaces)
    {
        zt::warn("conv_xmodel: source '%s' is incomplete or has inconsistent counts",
                 in.name.c_str());
        return false;
    }
    out = Iw8XModelRecord{};
    out.name = in.name;
    out.numsurfs = in.numSurfaces;
    out.numLods = in.numLods;
    out.collLod = (uint8_t)in.collLod; // IW8 collLod is u8; IW5 -1 -> 0xFF (no coll lod)
    out.numBones = in.numBones;
    out.numRootBones = in.numRootBones;
    out.numClientBones = 0;
    out.shadowCutoffLod = 6;
    out.lodRampType = in.lodRampType;
    out.flags = (uint32_t)in.flags; // zero-extend char -> u32
    out.contents = in.contents;
    out.scale = (in.scale != 0.f) ? in.scale : 1.f;
    out.radius = in.radius;
    for (int k = 0; k < 3; ++k)
    {
        out.boundsMid[k] = in.boundsMid[k];
        out.boundsHalf[k] = in.boundsHalf[k];
    }

    out.boneNames = in.boneNames; // [numBones]
    out.skeleton = in.skeleton;

    // per-LOD info
    const int nl = (in.numLods <= 4) ? in.numLods : 4;
    out.lods.reserve(nl);
    for (int i = 0; i < nl; ++i)
    {
        const dumpsrc::XModelLodDump &L = in.lods[i];
        Iw8LodInfo o;
        o.dist = L.dist;
        o.numsurfs = L.numSurfacesInLod;
        o.surfIndex = L.surfIndex;
        o.flags = 0; // IW8 XModelLodInfoFlags — none derivable from IW5
        // partBits: IW5 6×u32 (24B) -> IW8 8×u32 (32B), zero-extend the trailing 8 bytes.
        for (int k = 0; k < 6; ++k)
            o.partBits[k] = L.partBits[k];
        o.partBits[6] = 0;
        o.partBits[7] = 0;
        o.surfsName = L.surfsName; // XModelSurfs link (== the .xse asset name)
        out.lods.push_back(o);
    }

    // materials (cross-ref names) — [numsurfs]. Empty names (null materials) are kept as "" so the
    // handle slot count stays == numsurfs; the writer emits a NULL Material* for those.
    out.materials = in.materials;
    // Older engines do not carry IW8's per-surface high-mip streaming radii. A zero value is the
    // native IW8 sentinel for surfaces without a derived high-mip radius; the array itself must
    // still exist because Replay's model-to-material table reads every surface slot unconditionally.
    out.himipRadiusInvSq.assign(out.numsurfs, 0.0f);

    out.physPresetName = in.physPresetName;
    out.physCollmapName = in.physCollmapName;
    return true;
}

} // namespace convert::xmodel
