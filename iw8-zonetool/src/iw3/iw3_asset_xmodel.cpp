

#include "../convert/registry.h"
#include "../convert/xmodel_convert.h"
#include "../common/log.h"
#include "iw3_zone.h"
#include "iw3_structs.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <string>
#include <vector>

namespace convert {

namespace {

namespace cv = convert::xmodel_cv;

// Read a NUL-terminated string starting at absolute zone offset `at`. Bounded by zone size.
std::string readCString(iw3::LoadCtx& lc, size_t at) {
    std::string s;
    if (at == iw3::LoadCtx::npos)
        return s;
    const uint8_t* base = lc.base();
    size_t n = lc.size();
    for (size_t i = at; i < n; ++i) {
        char c = static_cast<char>(base[i]);
        if (c == '\0')
            break;
        s.push_back(c);
        if (s.size() > 4096)
            break; // sanity guard
    }
    return s;
}

// Resolve an iw3_ptr to an absolute zone offset. *follows set when the field was the -1 inline marker.
template <typename T> size_t resolvePtr(iw3::LoadCtx& lc, const iw3::iw3_ptr<T>& p, bool* follows) {
    return lc.resolve(p.off, follows);
}

// Read a string field that may be `follows` (consume inline at cursor) or aliased (read at offset).
// When `follows`, advances the main cursor past the string + NUL (loader behaviour).
std::string readStrField(iw3::LoadCtx& lc, const iw3::iw3_ptr<char>& p) {
    bool follows = false;
    size_t at = resolvePtr(lc, p, &follows);
    if (at == iw3::LoadCtx::npos)
        return {};
    std::string s = readCString(lc, at);
    if (follows)
        lc.seek(at + s.size() + 1); // consume inline payload + NUL
    return s;
}

} // namespace

void iw3_dump_xmodel(iw3::LoadCtx& lc, iw3sr::ZoneSource& zs) {
    // 1) Read the fixed IW3 XModel struct from the current cursor (the loader reads it whole first).
    const size_t structPos = lc.pos();
    iw3::XModel xm{};
    if (!lc.read(&xm, sizeof(xm))) {
        zt::err("iw3_dump_xmodel: short read of XModel struct @%zu", structPos);
        return;
    }

    cv::Record rec;
    rec.numBones = static_cast<uint8_t>(xm.numBones);
    rec.numRootBones = static_cast<uint8_t>(xm.numRootBones);
    rec.numsurfs = static_cast<uint16_t>(xm.numsurfs);
    rec.numLods = static_cast<uint8_t>(xm.numLods);
    rec.collLod = static_cast<int8_t>(xm.collLod);
    rec.lodRampType = static_cast<uint8_t>(xm.lodRampType);
    rec.flags = static_cast<uint32_t>(xm.flags);
    rec.contents = xm.contents;
    rec.scale = 1.0f; // IW3 XModel has no scale field; IW8 default is 1.0
    rec.radius = xm.radius;
    cv::boundsFromMinsMaxs(xm.mins, xm.maxs, rec.boundsMid, rec.boundsHalf);

    const int animBones = (xm.numBones > xm.numRootBones) ? (xm.numBones - xm.numRootBones) : 0;

    // 2) name (follows-inline in load order).
    rec.name = readStrField(lc, xm.name);
    if (rec.name.empty())
        rec.name = "xmodel_unnamed";

    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.boneNames, &follows);
        if (at != iw3::LoadCtx::npos && follows) {
            lc.seek(at + static_cast<size_t>(xm.numBones) * sizeof(uint16_t));
        }
        // Emit placeholder bone-name handles (numbers as strings) so the writer has the right count.
        rec.boneNames.reserve(rec.numBones);
        for (int i = 0; i < rec.numBones; ++i)
            rec.boneNames.push_back(std::string());
    }

    // 4) parentList[animBones] (u8).
    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.parentList, &follows);
        if (at != iw3::LoadCtx::npos) {
            rec.parentList.resize(animBones);
            for (int i = 0; i < animBones; ++i)
                rec.parentList[i] = lc.base()[at + i];
            if (follows)
                lc.seek(at + animBones);
        } else {
            rec.parentList.assign(animBones, 0);
        }
    }

    // 5) quats[4*animBones] (i16).
    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.quats, &follows);
        const size_t cnt = static_cast<size_t>(animBones) * 4;
        rec.quats.resize(cnt);
        if (at != iw3::LoadCtx::npos) {
            std::memcpy(rec.quats.data(), lc.base() + at, cnt * sizeof(int16_t));
            if (follows)
                lc.seek(at + cnt * sizeof(int16_t));
        }
    }

    // 6) trans[3*animBones] (f32).
    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.trans, &follows);
        const size_t cnt = static_cast<size_t>(animBones) * 3;
        rec.trans.resize(cnt);
        if (at != iw3::LoadCtx::npos) {
            std::memcpy(rec.trans.data(), lc.base() + at, cnt * sizeof(float));
            if (follows)
                lc.seek(at + cnt * sizeof(float));
        }
    }

    // 7) partClassification[numBones] (u8).
    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.partClassification, &follows);
        if (at != iw3::LoadCtx::npos) {
            rec.partClassification.resize(rec.numBones);
            for (int i = 0; i < rec.numBones; ++i)
                rec.partClassification[i] = lc.base()[at + i];
            if (follows)
                lc.seek(at + rec.numBones);
        }
    }

    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.baseMat, &follows);
        if (at != iw3::LoadCtx::npos && follows)
            lc.seek(at + static_cast<size_t>(xm.numBones) * sizeof(iw3::DObjAnimMat));
    }

    rec.surfsName = rec.name;
    {
        bool follows = false;
        size_t at = resolvePtr(lc, xm.surfs, &follows);
        if (at != iw3::LoadCtx::npos && follows)
            lc.seek(at + static_cast<size_t>(xm.numsurfs) * sizeof(iw3::XSurface));
    }

    // 10) materialHandles[numsurfs] : array of Material* (each a 4-byte aliased asset pointer). Resolve
    //     each to its Material asset, follow that material's name pointer, record the NAME. These names
    //     let the IW8 writer cross-reference the material assets in its zone.
    {
        bool follows = false;
        size_t mhAt = resolvePtr(lc, xm.materialHandles, &follows);
        rec.materials.reserve(rec.numsurfs);
        if (mhAt != iw3::LoadCtx::npos) {
            for (int i = 0; i < rec.numsurfs; ++i) {
                // each element is a 4-byte iw3_ptr<Material>
                uint32_t matPtrVal = 0;
                std::memcpy(&matPtrVal, lc.base() + mhAt + i * sizeof(uint32_t), sizeof(uint32_t));
                iw3::iw3_ptr<iw3::Material> mp{matPtrVal};
                bool mf = false;
                size_t matAt = resolvePtr(lc, mp, &mf);
                std::string matName;
                if (matAt != iw3::LoadCtx::npos && matAt + sizeof(iw3::Material) <= lc.size()) {
                    iw3::Material mat{};
                    std::memcpy(&mat, lc.base() + matAt, sizeof(mat));
                    // Material.name is the first field; read its string (aliased, not inline-consuming).
                    bool nf = false;
                    size_t nameAt = lc.resolve(mat.name.off, &nf);
                    matName = readCString(lc, nameAt);
                }
                if (matName.empty())
                    matName = rec.name + "_mtl" + std::to_string(i);
                rec.materials.push_back(matName);
            }
            if (follows)
                lc.seek(mhAt + static_cast<size_t>(rec.numsurfs) * sizeof(uint32_t));
        } else {
            for (int i = 0; i < rec.numsurfs; ++i)
                rec.materials.push_back(rec.name + "_mtl" + std::to_string(i));
        }
    }

    // 11) lodInfo[4] (inline in struct, already read). Convert the populated lods (numLods of them).
    const int nLods = (xm.numLods > 0 && xm.numLods <= 4) ? xm.numLods : 0;
    for (int i = 0; i < nLods; ++i) {
        cv::LodInfo li;
        li.dist = xm.lodInfo[i].dist;
        li.numsurfs = xm.lodInfo[i].numsurfs;
        li.surfIndex = xm.lodInfo[i].surfIndex;
        li.partBits = cv::partBitsFromIw3(xm.lodInfo[i].partBits);
        li.flags = 0; // IW8 XModelLodInfoFlags; IW3 has no direct equivalent -> 0
        rec.lods.push_back(li);
    }
    rec.numLods = static_cast<uint8_t>(nLods);

    // 12) Write the zone-source: manifest row (IW8 type "xmodel") + the JSON.
    cv::json j = cv::toJson(rec);
    if (!zs.addXModel(rec.name, j.dump(2)))
        zt::err("iw3_dump_xmodel: failed to write xmodel/%s.json", rec.name.c_str());
    else
        zt::info("iw3_dump_xmodel: dumped '%s' (bones=%u surfs=%u lods=%u mats=%zu)",
                 rec.name.c_str(), rec.numBones, rec.numsurfs, rec.numLods, rec.materials.size());
}

} // namespace convert
