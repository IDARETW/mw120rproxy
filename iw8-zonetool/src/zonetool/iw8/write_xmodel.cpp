#include "../../common/fs_util.h"
#include "../../common/log.h"
#include "../dumpsrc/xmodel_dump.h"
#include "iw8_focus_structs.h"
#include "iw8_structs.h"
#include "iw8_zone.h"
#include <cstring>
#include <string>

namespace iw8
{

namespace
{

namespace cx = convert::xmodel;

// stamp a tagged sentinel (or any 64-bit value) into a struct pointer slot without UB.
template <typename P> inline void stampPtr(P &slot, uint64_t v)
{
    std::memcpy(&slot, &v, sizeof(slot));
}

// Build the fixed iw8_focus::XModel struct (0x2B0) from the converted record. All //PTR fields get
// tagged sentinels; scalar fields get the real values. Returns the struct by value.
iw8_focus::XModel buildStruct(const cx::Iw8XModelRecord &rec)
{
    iw8_focus::XModel xm;
    std::memset(&xm, 0, sizeof(xm));

    // ---- scalar fields ----
    xm.numsurfs = rec.numsurfs;
    xm.numLods = rec.numLods;
    xm.collLod = rec.collLod;
    xm.shadowCutoffLod = rec.shadowCutoffLod;
    xm.numBones = rec.numBones;
    xm.numRootBones = rec.numRootBones;
    xm.numClientBones = rec.numClientBones;
    xm.flags = rec.flags;
    xm.contents = rec.contents;
    xm.scale = (rec.scale != 0.f) ? rec.scale : 1.f;
    xm.radius = rec.radius;
    xm.bounds.midPoint = {rec.boundsMid[0], rec.boundsMid[1], rec.boundsMid[2]};
    xm.bounds.halfSize = {rec.boundsHalf[0], rec.boundsHalf[1], rec.boundsHalf[2]};
    // numAimAssistBones / numClothAssets / mdaoVolumeCount / physicsUseCategory / impactType /
    // mdaoType / characterCollBoundsType / edgeLength / lgvData / physicsUsageCounter /
    // noScalePartBits: 0 (no IW5 source) -> all their //PTR/CNT pairs are null/zero (load-safe).

    // ---- pointer fields -> sentinels ----
    stampPtr(xm.name, PTR_FOLLOWS); // name follows
    stampPtr(xm.scriptableMoverDef, PTR_NULL);
    stampPtr(xm.proceduralBones, PTR_NULL);
    stampPtr(xm.dynamicBones, PTR_NULL);
    stampPtr(xm.aimAssistBones, PTR_NULL);                         // numAimAssistBones=0
    stampPtr(xm.boneNames, rec.numBones ? PTR_FOLLOWS : PTR_NULL); // [numBones] follows
    stampPtr(xm.parentList, PTR_NULL);                             // offline: not re-derived
    stampPtr(xm.quats, PTR_NULL);
    stampPtr(xm.trans, PTR_NULL);
    stampPtr(xm.partClassification, PTR_NULL);
    stampPtr(xm.baseMat, PTR_NULL);
    stampPtr(xm.ikHingeAxis, PTR_NULL);
    stampPtr(xm.reactiveMotionInfo, PTR_NULL);
    stampPtr(xm.materialHandles, rec.numsurfs ? PTR_FOLLOWS : PTR_NULL); // [numsurfs] follows
    stampPtr(xm.boneInfo, PTR_NULL);
    stampPtr(xm.himipRadiusInvSq, PTR_NULL);
    stampPtr(xm.physicsAsset, PTR_NULL);
    stampPtr(xm.physicsFXShape, PTR_NULL);
    stampPtr(xm.detailCollision, PTR_NULL);
    stampPtr(xm.clothAssets, PTR_NULL); // numClothAssets=0
    stampPtr(xm.blendShapeInfo, PTR_NULL);
    stampPtr(xm.mdaoVolumes, PTR_NULL); // mdaoVolumeCount=0
    stampPtr(xm.decalVolumesInfo, PTR_NULL);
    // _retailTail[8] stays zeroed (load-safe; not an active pointer in a minimal model).

    // ---- lodInfo[6] (inline in the struct) ----
    for (int i = 0; i < 6; ++i)
    {
        iw8_focus::XModelLodInfo &li = xm.lodInfo[i];
        std::memset(&li, 0, sizeof(li));
        if (i < (int)rec.lods.size())
        {
            const cx::Iw8LodInfo &s = rec.lods[i];
            li.dist = s.dist;
            li.numsurfs = s.numsurfs;
            li.surfIndex = s.surfIndex;
            li.flags = s.flags;
            std::memcpy(li.partBits, s.partBits.data(),
                        sizeof(uint32_t) * s.partBits.size()); // 8 u32 = 32B == partBits[32]
        }
        stampPtr(li.modelSurfsStaging, PTR_NULL); // XModelSurfs cross-ref (verify-phase alias)
        stampPtr(li.surfs, PTR_NULL);             // XSurface*    cross-ref (verify-phase alias)
    }
    return xm;
}

// The asset body callback: struct -> TEMP_PRELOAD, trailing name+arrays -> VIRTUAL.
void emitBody(ZoneWriter &zw, const cx::Iw8XModelRecord &rec)
{
    iw8_focus::XModel xm = buildStruct(rec);
    static_assert(sizeof(iw8_focus::XModel) == iw8sz::XMODEL, "XModel writer struct must be 0x2B0");

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zw.align(7);               // 8-align the struct
    zw.write(&xm, sizeof(xm)); // exactly 0x2B0 bytes

    zw.pushStream(XFILE_BLOCK_VIRTUAL);
    // name (follows)
    zw.writeStr(rec.name);

    // boneNames[numBones] : scr_string_t (u32). No live ScriptStringList offline -> zeroed handles
    // (0 = empty-string handle, load-safe). align to 4.
    if (rec.numBones)
    {
        zw.align(3);
        for (int i = 0; i < rec.numBones; ++i)
        {
            uint32_t h = 0;
            zw.write(&h, sizeof(h));
        }
    }
    // materialHandles[numsurfs] : Material* . Cross-ref aliasing unavailable -> each slot NULL
    // (the array region is present & walkable). align to 8 (pointer array).
    if (rec.numsurfs)
    {
        zw.align(7);
        for (int i = 0; i < rec.numsurfs; ++i)
        {
            uint64_t v = PTR_NULL;
            zw.write(&v, sizeof(v));
        }
    }
    zw.popStream(); // VIRTUAL -> TEMP_PRELOAD
    zw.popStream(); // TEMP_PRELOAD -> (caller's stream, VIRTUAL)
}

} // namespace

void writeXModel(ZoneWriter &zw, const cx::Iw8XModelRecord &rec)
{
    if (rec.name.empty())
    {
        zt::err("writeXModel: empty record name");
        return;
    }
    // capture rec by value so the deferred body callback owns its data.
    cx::Iw8XModelRecord r = rec;
    zw.add(ASSET_TYPE_XMODEL, r.name, [r](ZoneWriter &w) { emitBody(w, r); });
    zt::info("writeXModel: registered '%s' (0x2B0; surfs=%u lods=%u bones=%u) "
             "[material/surfs cross-refs NULL — verify-phase wiring]",
             r.name.c_str(), r.numsurfs, r.numLods, r.numBones);
}

bool addXModelFromDump(ZoneWriter &zw, const std::string &dumpDir, const std::string &name)
{
    dumpsrc::XModelDumpFull dump;
    if (!dumpsrc::readXModel(dumpDir, name, dump))
    {
        zt::warn("addXModelFromDump: '%s' not read — skipped (asset)", name.c_str());
        return false;
    }
    cx::Iw8XModelRecord rec;
    if (!cx::toIw8(dump, rec))
    {
        zt::warn("addXModelFromDump: '%s' convert failed — skipped", name.c_str());
        return false;
    }
    writeXModel(zw, rec);
    return true;
}

} // namespace iw8
