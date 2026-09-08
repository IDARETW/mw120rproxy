

#include "../convert/registry.h"
#include "../convert/xmodel_convert.h"
#include "../common/log.h"
#include "iw8_zone.h"
#include "iw8_structs.h"
#include "iw8_focus_structs.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <string>
#include <vector>

namespace convert {

namespace {

namespace cv = convert::xmodel_cv;
using iw8::PTR_NULL;
using iw8::PTR_FOLLOWS;

// Emit the XModel asset body (struct + trailing data) into the writer's CURRENT stream (VIRTUAL),
// matching the dev Load_XModel read order. `rec` is the converted zone-source record.
void emitXModelBody(iw8::ZoneWriter& zw, const cv::Record& rec) {
    const int animBones = (rec.numBones > rec.numRootBones) ? (rec.numBones - rec.numRootBones) : 0;

    // ---- 1) build the fixed XModel struct in memory, stamp sentinels for every pointer field -------
    iw8_focus::XModel xm{};
    std::memset(&xm, 0, sizeof(xm));

    xm.numsurfs = rec.numsurfs;
    xm.numLods = rec.numLods;
    xm.collLod = static_cast<uint8_t>(rec.collLod);
    xm.numBones = rec.numBones;
    xm.numRootBones = rec.numRootBones;
    xm.numClientBones = 0;
    xm.flags = rec.flags;
    xm.contents = rec.contents;
    xm.scale = (rec.scale != 0.f) ? rec.scale : 1.0f;
    xm.radius = rec.radius;
    xm.bounds.midPoint = {rec.boundsMid[0], rec.boundsMid[1], rec.boundsMid[2]};
    xm.bounds.halfSize = {rec.boundsHalf[0], rec.boundsHalf[1], rec.boundsHalf[2]};

    auto P = [](void*& slot, uint64_t v) {
        std::memcpy(&slot, &v, sizeof(slot));
    };
    auto Pc = [](const char*& slot, uint64_t v) {
        std::memcpy(&slot, &v, sizeof(slot));
    };

    Pc(xm.name, PTR_FOLLOWS);
    P(reinterpret_cast<void*&>(xm.scriptableMoverDef), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.proceduralBones), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.dynamicBones), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.aimAssistBones), PTR_NULL); // numAimAssistBones=0
    P(reinterpret_cast<void*&>(xm.boneNames), (rec.numBones ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.parentList), (animBones ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.quats), (animBones ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.trans), (animBones ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.partClassification),
      (!rec.partClassification.empty() ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.baseMat), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.ikHingeAxis), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.reactiveMotionInfo), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.materialHandles), (rec.numsurfs ? PTR_FOLLOWS : PTR_NULL));
    P(reinterpret_cast<void*&>(xm.boneInfo), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.himipRadiusInvSq), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.physicsAsset), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.physicsFXShape), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.detailCollision), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.clothAssets), PTR_NULL); // numClothAssets=0
    P(reinterpret_cast<void*&>(xm.blendShapeInfo), PTR_NULL);
    P(reinterpret_cast<void*&>(xm.mdaoVolumes), PTR_NULL); // mdaoVolumeCount=0
    P(reinterpret_cast<void*&>(xm.decalVolumesInfo), PTR_NULL);

    // lodInfo[6] (inline in the struct): stamp each LOD's scalar fields + null its two cross-ref ptrs.
    for (int i = 0; i < 6; ++i) {
        iw8_focus::XModelLodInfo& li = xm.lodInfo[i];
        std::memset(&li, 0, sizeof(li));
        if (i < (int)rec.lods.size()) {
            li.dist = rec.lods[i].dist;
            li.numsurfs = rec.lods[i].numsurfs;
            li.surfIndex = rec.lods[i].surfIndex;
            li.flags = rec.lods[i].flags;
            std::memcpy(li.partBits, rec.lods[i].partBits.data(),
                        sizeof(uint32_t) * rec.lods[i].partBits.size());
        }
        P(reinterpret_cast<void*&>(li.modelSurfsStaging),
          PTR_NULL);                                     // XModelSurfs cross-ref (verify-phase)
        P(reinterpret_cast<void*&>(li.surfs), PTR_NULL); // XSurface*  cross-ref (verify-phase)
    }

    // ---- 2) write the struct, then trailing data in load-read order (current stream = VIRTUAL) ------
    zw.align(7);
    zw.write(&xm, sizeof(xm)); // exactly 0x2B0 bytes

    // name (follows)
    zw.writeStr(rec.name);

    if (rec.numBones) {
        zw.align(3);
        for (int i = 0; i < rec.numBones; ++i) {
            uint32_t h = 0;
            zw.write(&h, sizeof(h));
        }
    }
    // parentList[animBones] (u8)
    if (animBones) {
        zw.align(0);
        for (int i = 0; i < animBones; ++i) {
            uint8_t v = (i < (int)rec.parentList.size()) ? rec.parentList[i] : 0;
            zw.write(&v, 1);
        }
    }
    // quats[4*animBones] (i16)
    if (animBones) {
        zw.align(1);
        const size_t cnt = (size_t)animBones * 4;
        for (size_t i = 0; i < cnt; ++i) {
            int16_t v = (i < rec.quats.size()) ? rec.quats[i] : 0;
            zw.write(&v, sizeof(v));
        }
    }
    // trans[3*animBones] (f32)
    if (animBones) {
        zw.align(3);
        const size_t cnt = (size_t)animBones * 3;
        for (size_t i = 0; i < cnt; ++i) {
            float v = (i < rec.trans.size()) ? rec.trans[i] : 0.f;
            zw.write(&v, sizeof(v));
        }
    }
    // partClassification[numBones] (u8) — only if present in the source
    if (!rec.partClassification.empty()) {
        zw.align(0);
        for (int i = 0; i < rec.numBones; ++i) {
            uint8_t v = (i < (int)rec.partClassification.size()) ? rec.partClassification[i] : 0;
            zw.write(&v, 1);
        }
    }
    // materialHandles[numsurfs] : array of Material* . Cross-ref aliasing not yet available -> each
    // slot NULL (load-safe; the array region is still present & walkable). align to 8 (pointer array).
    if (rec.numsurfs) {
        zw.align(7);
        for (int i = 0; i < rec.numsurfs; ++i) {
            uint64_t v = PTR_NULL;
            zw.write(&v, sizeof(v));
        }
    }
    // (baseMat / boneInfo / himipRadiusInvSq / physics / lod surfs: all null -> nothing to emit)
}

} // namespace

void iw8_write_xmodel(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name) {
    if (!name) {
        zt::err("iw8_write_xmodel: null name");
        return;
    }

    std::string jsonText;
    if (!zs.getXModel(name, jsonText)) {
        zt::err("iw8_write_xmodel: cannot read xmodel/%s.json", name);
        return;
    }

    cv::Record rec;
    cv::json j;
    try {
        j = cv::json::parse(jsonText);
    } catch (const std::exception& e) {
        zt::err("iw8_write_xmodel: bad JSON for '%s': %s", name, e.what());
        return;
    }
    if (!cv::fromJson(j, rec)) {
        zt::err("iw8_write_xmodel: schema parse failed for '%s'", name);
        return;
    }
    if (rec.name.empty())
        rec.name = name;

    int repaired = cv::normalize(rec);
    if (repaired)
        zt::warn("iw8_write_xmodel: '%s' had %d field(s) clamped to declared counts", name,
                 repaired);

    emitXModelBody(zw, rec);
    zt::info("iw8_write_xmodel: emitted '%s' (0x2B0 struct; bones=%u surfs=%u lods=%u) "
             "[material/surfs cross-refs NULL — verify-phase wiring]",
             rec.name.c_str(), rec.numBones, rec.numsurfs, rec.numLods);
}

} // namespace convert
