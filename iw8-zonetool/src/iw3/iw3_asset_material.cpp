// iw3/asset_material.cpp — Stage-A IW3 Material reader + zone-source dumper (FOCUS family: material).
// Replaces the no-op convert::iw3_dump_material stub in iw3_assets_stub.cpp (that stub is deleted for
// this family to avoid a duplicate-symbol link error; see the stub file's own "REPLACE these" note).
//
// Reproduces zonetool-develop IW3 IMaterial::dump (src/IW3/Assets/Material.cpp) OFFLINE: instead of
// reading a game-resident Material*, it reads the IW3 Material struct + its pointer graph straight from
// the inflated zone buffer via the LoadCtx tagged-pointer resolve(). The emitted JSON MIRRORS the
// zonetool IW3 dump shape so a downstream IW8/IW-line reader recognizes it:
//
//   {
//     "name": "<material name>",
//     "techniqueSet->name": "iw3/<techset>"   (only if techniqueSet != null; techset deferred -> omitted
//                                               when the pointer can't be resolved to a name)
//     "gameFlags": <int>, "animationX": <int>, "animationY": <int>, "sortKey": <int>,
//     "unknown": 0, "surfaceTypeBits": <int>, "stateFlags": <int>, "cameraRegion": <int>,
//     "constantTable": [ { "name": "<12>", "nameHash": <u32>, "literal": [f,f,f,f] }, ... ],
//     "stateMap":      [ [loadBits0, loadBits1], ... ],
//     "maps":          [ { "image": "<imgname>", "semantic": <int>, "sampleState": <int>,
//                          "lastCharacter": <char>, "firstCharacter": <char>, "typeHash": <u32> }, ... ]
//   }
//
// The LoadCtx is positioned (by the driver, P4/P5) at the start of the serialized Material struct. This
// reader does NOT advance the driver's primary cursor past the struct graph (the IW3 driver walk owns
// cursor advancement); it reads via absolute offsets resolved from the struct's own pointer fields. It
// records the asset in the manifest as IW8 type "material".
#include "material_dump.h"
#include "../convert/registry.h"
#include "../convert/material.h"
#include "../zonesrc/zone_source.h"
#include "../common/log.h"
#include "../common/json.hpp"
#include <cstring>

using nlohmann::ordered_json;

namespace iw3mtl {

// ---- absolute-offset readers (no cursor advance) -------------------------------------------------
bool readAt(iw3::LoadCtx& lc, size_t abs, void* dst, size_t n) {
    if (abs == iw3::LoadCtx::npos)
        return false;
    if (abs + n > lc.size())
        return false;
    std::memcpy(dst, lc.base() + abs, n);
    return true;
}

std::string readCStrAt(iw3::LoadCtx& lc, size_t abs) {
    if (abs == iw3::LoadCtx::npos || abs >= lc.size())
        return std::string();
    const char* p = reinterpret_cast<const char*>(lc.base() + abs);
    size_t maxLen = lc.size() - abs;
    size_t len = 0;
    while (len < maxLen && p[len] != '\0')
        ++len;
    return std::string(p, len);
}

size_t resolvePtrAt(iw3::LoadCtx& lc, size_t ptrFieldAbs, bool* isNull, bool* isFollows) {
    if (isNull)
        *isNull = false;
    if (isFollows)
        *isFollows = false;
    uint32_t v = 0;
    if (!readScalarAt(lc, ptrFieldAbs, v)) {
        if (isNull)
            *isNull = true;
        return iw3::LoadCtx::npos;
    }
    if (v == 0u) {
        if (isNull)
            *isNull = true;
        return iw3::LoadCtx::npos;
    }
    bool follows = false;
    size_t target = lc.resolve(v, &follows);
    if (follows && isFollows)
        *isFollows = true;
    return target;
}

std::string readImageName(iw3::LoadCtx& lc, size_t imageBaseAbs) {
    if (imageBaseAbs == iw3::LoadCtx::npos)
        return std::string();
    size_t nameAbs = resolvePtrAt(lc, imageBaseAbs + off::img_name);
    return readCStrAt(lc, nameAbs);
}

} // namespace iw3mtl

namespace convert {

void iw3_dump_material(iw3::LoadCtx& lc, iw3sr::ZoneSource& zs) {
    using namespace iw3mtl;

    const size_t matBase = lc.pos(); // driver positions cursor at the Material struct start

    // --- header scalars (absolute offsets within the struct) ---
    size_t nameAbs = resolvePtrAt(lc, matBase + off::name);
    std::string name = readCStrAt(lc, nameAbs);
    if (name.empty()) {
        zt::warn("iw3_dump material: empty/unresolved name at zone off 0x%zX — skipping", matBase);
        return;
    }

    uint8_t gameFlags = 0, sortKey = 0, animationX = 0, animationY = 0;
    uint8_t numMaps = 0, constantCount = 0, stateBitsCount = 0, stateFlags = 0, cameraRegion = 0;
    uint32_t surfaceTypeBits = 0;
    readScalarAt(lc, matBase + off::gameFlags, gameFlags);
    readScalarAt(lc, matBase + off::sortKey, sortKey);
    readScalarAt(lc, matBase + off::animationX, animationX);
    readScalarAt(lc, matBase + off::animationY, animationY);
    readScalarAt(lc, matBase + off::surfaceTypeBits, surfaceTypeBits);
    readScalarAt(lc, matBase + off::numMaps, numMaps);
    readScalarAt(lc, matBase + off::constantCount, constantCount);
    readScalarAt(lc, matBase + off::stateBitsCount, stateBitsCount);
    readScalarAt(lc, matBase + off::stateFlags, stateFlags);
    readScalarAt(lc, matBase + off::cameraRegion, cameraRegion);

    ordered_json matdata;
    matdata["name"] = name;

    {
        bool tsNull = false;
        size_t tsBase = resolvePtrAt(lc, matBase + off::techniqueSet, &tsNull);
        if (!tsNull && tsBase != iw3::LoadCtx::npos) {
            // IW3 MaterialTechniqueSet.name is at offset 0 of the techset struct (a const char* ptr).
            size_t tsNameAbs = resolvePtrAt(lc, tsBase + 0);
            std::string tsName = readCStrAt(lc, tsNameAbs);
            if (!tsName.empty())
                matdata["techniqueSet->name"] = std::string("iw3/") + tsName;
        }
    }

    matdata["gameFlags"] = static_cast<int>(gameFlags);
    matdata["animationX"] = static_cast<int>(animationX);
    matdata["animationY"] = static_cast<int>(animationY);
    matdata["sortKey"] = static_cast<int>(sortKey);
    matdata["unknown"] = 0;
    matdata["surfaceTypeBits"] = static_cast<int>(surfaceTypeBits);
    matdata["stateFlags"] = static_cast<int>(stateFlags);
    matdata["cameraRegion"] = static_cast<int>(cameraRegion);

    // --- constantTable[constantCount] (MaterialConstantDef, stride 0x20) ---
    {
        ordered_json carr = ordered_json::array();
        bool ctNull = false;
        size_t ctBase = resolvePtrAt(lc, matBase + off::constantTable, &ctNull);
        if (!ctNull && ctBase != iw3::LoadCtx::npos) {
            for (uint8_t i = 0; i < constantCount; ++i) {
                size_t e = ctBase + static_cast<size_t>(i) * off::CD_SIZE;
                uint32_t nameHash = 0;
                char cname[13] = {0};
                float literal[4] = {0, 0, 0, 0};
                readScalarAt(lc, e + off::cd_nameHash, nameHash);
                readAt(lc, e + off::cd_name, cname, 12);
                readAt(lc, e + off::cd_literal, literal, sizeof(literal));
                ordered_json cent;
                cent["name"] = std::string(cname); // NUL-bounded within the 12-byte field
                cent["nameHash"] = nameHash;
                cent["literal"] = {literal[0], literal[1], literal[2], literal[3]};
                carr.push_back(cent);
            }
        }
        matdata["constantTable"] = carr;
    }

    // --- stateMap[stateBitsCount] (GfxStateBits: loadBits[2], stride 0x08) ---
    {
        ordered_json sarr = ordered_json::array();
        bool sbNull = false;
        size_t sbBase = resolvePtrAt(lc, matBase + off::stateMap, &sbNull);
        if (!sbNull && sbBase != iw3::LoadCtx::npos) {
            for (uint8_t i = 0; i < stateBitsCount; ++i) {
                size_t e = sbBase + static_cast<size_t>(i) * off::SB_SIZE;
                uint32_t lb0 = 0, lb1 = 0;
                readScalarAt(lc, e + 0, lb0);
                readScalarAt(lc, e + 4, lb1);
                sarr.push_back({lb0, lb1});
            }
        }
        matdata["stateMap"] = sarr;
    }

    // --- maps[numMaps] (MaterialTextureDef, stride 0x0C) ---
    {
        ordered_json marr = ordered_json::array();
        bool mNull = false;
        size_t mBase = resolvePtrAt(lc, matBase + off::maps, &mNull);
        if (!mNull && mBase != iw3::LoadCtx::npos) {
            for (uint8_t i = 0; i < numMaps; ++i) {
                size_t e = mBase + static_cast<size_t>(i) * off::TD_SIZE;
                uint32_t typeHash = 0;
                int8_t firstChar = 0, secondLast = 0, sampleState = 0, semantic = 0;
                readScalarAt(lc, e + off::td_typeHash, typeHash);
                readScalarAt(lc, e + off::td_firstChar, firstChar);
                readScalarAt(lc, e + off::td_secondLast, secondLast);
                readScalarAt(lc, e + off::td_sampleState, sampleState);
                readScalarAt(lc, e + off::td_semantic, semantic);

                std::string imgName;
                size_t imgBase = resolvePtrAt(lc, e + off::td_image);
                if (imgBase != iw3::LoadCtx::npos) {
                    if (convert::mtl::iw3_semantic_is_water(static_cast<uint8_t>(semantic))) {

                        constexpr size_t water_image_off = 0x40;
                        size_t innerImgBase = resolvePtrAt(lc, imgBase + water_image_off);
                        imgName = readImageName(lc, innerImgBase);
                    } else {
                        imgName = readImageName(lc, imgBase);
                    }
                }

                ordered_json image;
                image["image"] = imgName;
                image["semantic"] = static_cast<int>(semantic);
                image["sampleState"] = static_cast<int>(sampleState);
                image["lastCharacter"] = static_cast<int>(secondLast);
                image["firstCharacter"] = static_cast<int>(firstChar);
                image["typeHash"] = typeHash;
                marr.push_back(image);
            }
        }
        matdata["maps"] = marr;
    }

    // --- write to zone-source (records manifest "material,<name>") ---
    std::string json = matdata.dump(4);
    if (!zs.addMaterial(name, json)) {
        zt::err("iw3_dump material '%s': failed to write zone-source JSON", name.c_str());
        return;
    }
    zt::info("iw3_dump material '%s': %u maps, %u const, %u statebits -> %s", name.c_str(), numMaps,
             constantCount, stateBitsCount, zs.materialPath(name).c_str());
}

} // namespace convert
