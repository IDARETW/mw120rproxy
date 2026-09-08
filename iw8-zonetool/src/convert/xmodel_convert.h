

#pragma once
#include "../common/json.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace convert::xmodel_cv {

using json = nlohmann::json;

// One LOD's converted fields (IW3 XModelLodInfo -> IW8 XModelLodInfo subset the writer stamps).
struct LodInfo {
    float dist = 0.f;
    uint16_t numsurfs = 0;
    uint16_t surfIndex = 0;
    std::array<uint32_t, 8> partBits{}; // IW8 DObjPartBits is 32 bytes = 8 u32 (IW3 had 4 u32/16B)
    uint8_t flags = 0;
};

// The full converted xmodel record (the JSON's typed form). Reader fills it from IW3; writer fills it
// from the JSON; both go through to/from json below so the on-disk shape has ONE definition.
struct Record {
    std::string name;
    uint8_t numBones = 0;
    uint8_t numRootBones = 0;
    uint16_t numsurfs = 0;
    uint8_t numLods = 0;
    int8_t collLod = 0;
    uint8_t lodRampType = 0;
    uint32_t flags = 0;
    int32_t contents = 0;
    float scale = 1.0f;
    float radius = 0.f;
    float boundsMid[3] = {0, 0, 0};
    float boundsHalf[3] = {0, 0, 0};
    std::vector<std::string> boneNames;      // [numBones]
    std::vector<uint8_t> parentList;         // [numBones-numRootBones]
    std::vector<int16_t> quats;              // [4*(numBones-numRootBones)]
    std::vector<float> trans;                // [3*(numBones-numRootBones)]
    std::vector<uint8_t> partClassification; // [numBones] (may be empty)
    std::vector<LodInfo> lods;               // [numLods]
    std::vector<std::string> materials;      // [numsurfs]
    std::string surfsName;                   // XModelSurfs asset name for lod0
};

// ---- IW3 mins/maxs -> IW8 Bounds{midPoint, halfSize} ---------------------------------------------
inline void
boundsFromMinsMaxs(const float mins[3], const float maxs[3], float midOut[3], float halfOut[3]) {
    for (int i = 0; i < 3; ++i) {
        midOut[i] = (maxs[i] + mins[i]) * 0.5f;
        halfOut[i] = (maxs[i] - mins[i]) * 0.5f;
    }
}

// ---- IW3 lod partBits[4] (16B) -> IW8 partBits[8] (32B), zero-extended -----------------------------
inline std::array<uint32_t, 8> partBitsFromIw3(const int32_t in4[4]) {
    std::array<uint32_t, 8> out{};
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<uint32_t>(in4[i]);
    return out;
}

// ---- JSON (de)serialize: ONE definition of the on-disk schema --------------------------------------
inline json toJson(const Record& r) {
    json j;
    j["name"] = r.name;
    j["numBones"] = r.numBones;
    j["numRootBones"] = r.numRootBones;
    j["numsurfs"] = r.numsurfs;
    j["numLods"] = r.numLods;
    j["collLod"] = r.collLod;
    j["lodRampType"] = r.lodRampType;
    j["flags"] = r.flags;
    j["contents"] = r.contents;
    j["scale"] = r.scale;
    j["radius"] = r.radius;
    j["bounds"] = {{"midPoint", {r.boundsMid[0], r.boundsMid[1], r.boundsMid[2]}},
                   {"halfSize", {r.boundsHalf[0], r.boundsHalf[1], r.boundsHalf[2]}}};
    j["boneNames"] = r.boneNames;
    j["parentList"] = r.parentList;
    j["quats"] = r.quats;
    j["trans"] = r.trans;
    j["partClassification"] = r.partClassification;
    json lods = json::array();
    for (const auto& l : r.lods) {
        lods.push_back({{"dist", l.dist},
                        {"numsurfs", l.numsurfs},
                        {"surfIndex", l.surfIndex},
                        {"partBits", l.partBits},
                        {"flags", l.flags}});
    }
    j["lods"] = lods;
    j["materials"] = r.materials;
    j["surfsName"] = r.surfsName;
    return j;
}

inline bool fromJson(const json& j, Record& r) {
    try {
        r.name = j.value("name", std::string());
        r.numBones = j.value("numBones", 0);
        r.numRootBones = j.value("numRootBones", 0);
        r.numsurfs = j.value("numsurfs", 0);
        r.numLods = j.value("numLods", 0);
        r.collLod = j.value("collLod", 0);
        r.lodRampType = j.value("lodRampType", 0);
        r.flags = j.value("flags", 0u);
        r.contents = j.value("contents", 0);
        r.scale = j.value("scale", 1.0f);
        r.radius = j.value("radius", 0.f);
        if (j.contains("bounds")) {
            auto mid = j["bounds"].value("midPoint", std::vector<float>{0, 0, 0});
            auto half = j["bounds"].value("halfSize", std::vector<float>{0, 0, 0});
            for (int i = 0; i < 3 && i < (int)mid.size(); ++i)
                r.boundsMid[i] = mid[i];
            for (int i = 0; i < 3 && i < (int)half.size(); ++i)
                r.boundsHalf[i] = half[i];
        }
        r.boneNames = j.value("boneNames", std::vector<std::string>{});
        r.parentList = j.value("parentList", std::vector<uint8_t>{});
        r.quats = j.value("quats", std::vector<int16_t>{});
        r.trans = j.value("trans", std::vector<float>{});
        r.partClassification = j.value("partClassification", std::vector<uint8_t>{});
        r.materials = j.value("materials", std::vector<std::string>{});
        r.surfsName = j.value("surfsName", std::string());
        if (j.contains("lods")) {
            for (const auto& lj : j["lods"]) {
                LodInfo l;
                l.dist = lj.value("dist", 0.f);
                l.numsurfs = lj.value("numsurfs", 0);
                l.surfIndex = lj.value("surfIndex", 0);
                auto pb = lj.value("partBits", std::vector<uint32_t>{});
                for (int i = 0; i < 8 && i < (int)pb.size(); ++i)
                    l.partBits[i] = pb[i];
                l.flags = lj.value("flags", 0);
                r.lods.push_back(l);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Validate/clamp a Record's array lengths to its declared counts (writer-side safety). Defined in
// xmodel.cpp. Returns the number of fields repaired (0 = clean).
int normalize(Record& r);

} // namespace convert::xmodel_cv
