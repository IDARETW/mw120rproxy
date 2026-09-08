

#include "material_write.h"
#include "iw8_zone.h"
#include "../convert/registry.h"
#include "../convert/material.h"
#include "../zonesrc/zone_source.h"
#include "../common/log.h"
#include "../common/json.hpp"
#include <cstring>

using nlohmann::json;
using namespace iw8_focus;

namespace iw8mtl {

static uint8_t slot_for(size_t arrayIndex) {
    return static_cast<uint8_t>(arrayIndex);
}

// Parse materials/<name>.json into a ParsedMaterial (scalars + texture/constant/submaterial lists).
// Returns false (and logs) on malformed JSON / missing file. Accepts BOTH the IW3 dump key set
// ("maps","gameFlags",...) and is tolerant of missing optional keys.
static bool parse_material(iw3sr::ZoneSource& zs, const std::string& name, ParsedMaterial& out) {
    std::string text;
    if (!zs.getMaterial(name, text)) {
        zt::err("iw8_write material '%s': cannot read zone-source JSON %s", name.c_str(),
                zs.materialPath(name).c_str());
        return false;
    }

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        zt::err("iw8_write material '%s': JSON parse error: %s", name.c_str(), e.what());
        return false;
    }

    auto getU8 = [&](const char* k, uint8_t def) -> uint8_t {
        return (j.contains(k) && !j[k].is_null()) ? static_cast<uint8_t>(j[k].get<int>()) : def;
    };
    auto getU32 = [&](const char* k, uint32_t def) -> uint32_t {
        return (j.contains(k) && !j[k].is_null()) ? j[k].get<uint32_t>() : def;
    };

    out.name = j.value("name", name);

    Material& m = out.mat;
    std::memset(&m, 0, sizeof(m));

    // --- scalar field conversion (IW3 -> IW8) ---
    uint8_t iw3GameFlags = getU8("gameFlags", 0);
    uint8_t iw3SortKey = getU8("sortKey", 0);
    uint8_t iw3CameraRegion = getU8("cameraRegion", 0);

    m.contents = 0;                                // IW3 has no direct content bits here
    m.surfaceFlags = getU32("surfaceTypeBits", 0); // IW3 surfaceTypeBits -> IW8 surfaceFlags
    m.maxDisplacement = 0.0f;
    m.materialType = convert::mtl::iw3_to_iw8_material_type(iw3GameFlags);
    m.cameraRegion = convert::mtl::iw3_to_iw8_camera_region(iw3CameraRegion);
    m.sortKey = convert::mtl::iw3_to_iw8_sortkey(iw3SortKey);
    m._u7 = 0;
    m.packedAtlasDataSize = 0;
    m.textureAtlasRowCount = getU8("textureAtlasRowCount", 0);
    m.textureAtlasColumnCount = getU8("textureAtlasColumnCount", 0);
    // drawSurf[16] stays zeroed (runtime-built render key; zero is load-safe).

    // --- textureTable (IW3 "maps", fallback "textureTable") ---
    const json* tex = nullptr;
    if (j.contains("maps") && j["maps"].is_array())
        tex = &j["maps"];
    else if (j.contains("textureTable") && j["textureTable"].is_array())
        tex = &j["textureTable"];
    if (tex) {
        size_t idx = 0;
        for (const auto& e : *tex) {
            TexBinding b{};
            uint8_t iw3Sem = e.contains("semantic") && !e["semantic"].is_null()
                                 ? static_cast<uint8_t>(e["semantic"].get<int>())
                                 : 0;
            b.semantic = convert::mtl::iw3_to_iw8_semantic(iw3Sem);
            b.index = slot_for(idx);
            b.imageName = e.value("image", std::string());
            b.samplerState = e.contains("sampleState") && !e["sampleState"].is_null()
                                 ? static_cast<uint8_t>(e["sampleState"].get<int>())
                                 : (e.contains("samplerState") && !e["samplerState"].is_null()
                                        ? static_cast<uint8_t>(e["samplerState"].get<int>())
                                        : 0);
            b.firstChar = e.contains("firstCharacter") && !e["firstCharacter"].is_null()
                              ? static_cast<int8_t>(e["firstCharacter"].get<int>())
                              : 0;
            b.lastChar = e.contains("lastCharacter") && !e["lastCharacter"].is_null()
                             ? static_cast<int8_t>(e["lastCharacter"].get<int>())
                             : 0;
            b.typeHash = e.contains("typeHash") && !e["typeHash"].is_null()
                             ? e["typeHash"].get<uint32_t>()
                             : 0;
            out.textures.push_back(std::move(b));
            ++idx;
        }
    }

    // --- constantTable ---
    if (j.contains("constantTable") && j["constantTable"].is_array()) {
        for (const auto& e : j["constantTable"]) {
            MaterialConstantDef c{};
            std::memset(&c, 0, sizeof(c));

            c.index = static_cast<uint8_t>(out.constants.size());
            if (e.contains("literal") && e["literal"].is_array()) {
                const auto& lit = e["literal"];
                for (int k = 0; k < 4 && k < static_cast<int>(lit.size()); ++k)
                    c.literal.v[k] = lit[k].is_null() ? 0.0f : lit[k].get<float>();
            }
            out.constants.push_back(c);
        }
    }

    // --- subMaterials ---
    if (j.contains("subMaterials") && j["subMaterials"].is_array()) {
        for (const auto& e : j["subMaterials"])
            if (e.is_string())
                out.subMaterials.push_back(e.get<std::string>());
    }

    // counts
    m.textureCount = static_cast<uint8_t>(out.textures.size());
    m.constantCount = static_cast<uint8_t>(out.constants.size());
    m.constantBufferCount = 0;
    m.layerCount = static_cast<uint8_t>(out.subMaterials.size());

    (void)getU32; // (kept for symmetry / future fields)
    return true;
}

} // namespace iw8mtl

namespace convert {

void iw8_write_material(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name) {
    using namespace iw8mtl;
    if (!name) {
        zt::err("iw8_write material: null name");
        return;
    }

    ParsedMaterial pm;
    if (!parse_material(zs, name, pm))
        return;

    // Register the asset; the body runs in VIRTUAL(8) (driver frames XAsset.header=-3 first).
    zw.add(ASSET_TYPE_MATERIAL, pm.name, [pm](iw8::ZoneWriter& w) {
        // ---- 1) the 0x78 Material struct, pointer fields stamped ----
        Material m = pm.mat; // scalar fields already filled by parse_material
        // Stamp pointer slots with raw sentinels (the struct's pointer fields are 8 bytes on host).
        m.name = reinterpret_cast<const char*>(iw8::PTR_FOLLOWS); // 0x00 follows
        m.packedAtlasData = nullptr;                              // 0x38 null
        m.techniqueSet = nullptr;                                 // 0x40 null (techset deferred)
        m.textureTable = pm.textures.empty()
                             ? nullptr
                             : reinterpret_cast<MaterialTextureDef*>(iw8::PTR_FOLLOWS); // 0x48
        m.constantTable = pm.constants.empty()
                              ? nullptr
                              : reinterpret_cast<MaterialConstantDef*>(iw8::PTR_FOLLOWS); // 0x50
        m.decalVolumeMaterial = nullptr; // 0x58 null
        m.constantBufferIndex = nullptr; // 0x60 null
        m.constantBufferTable = nullptr; // 0x68 null
        m.subMaterials = pm.subMaterials.empty()
                             ? nullptr
                             : reinterpret_cast<const char**>(iw8::PTR_FOLLOWS); // 0x70
        static_assert(sizeof(Material) == 0x78, "IW8 Material must be 0x78 (g_assetSizes[11])");
        w.write(&m, sizeof(m));

        // ---- 2) followed payloads in STRUCT-FIELD ORDER ----
        // name (0x00)
        w.writeStr(pm.name.c_str());

        // textureTable (0x48): MaterialTextureDef[textureCount], 8-aligned (8-byte image ptr).
        if (!pm.textures.empty()) {
            w.align(7);
            for (const auto& tb : pm.textures) {
                MaterialTextureDef td{};
                std::memset(&td, 0, sizeof(td));
                td.index = tb.index;

                td.image = nullptr;
                static_assert(sizeof(MaterialTextureDef) == 0x10,
                              "MaterialTextureDef must be 0x10");
                w.write(&td, sizeof(td));
            }
        }

        // constantTable (0x50): MaterialConstantDef[constantCount], stride 0x14, 4-aligned.
        if (!pm.constants.empty()) {
            w.align(3);
            for (const auto& c : pm.constants) {
                static_assert(sizeof(MaterialConstantDef) == 0x14,
                              "MaterialConstantDef stride 0x14");
                w.write(&c, sizeof(c)); // all-scalar; no pointer to stamp
            }
        }

        // subMaterials (0x70): const char*[layerCount] then each string (follows), 8-aligned.
        if (!pm.subMaterials.empty()) {
            w.align(7);
            for (size_t i = 0; i < pm.subMaterials.size(); ++i) {
                uint64_t p = iw8::PTR_FOLLOWS; // each element string follows inline
                w.write(&p, sizeof(p));
            }
            for (const auto& s : pm.subMaterials)
                w.writeStr(s.c_str());
        }
    });

    zt::info("iw8_write material '%s': %zu tex, %zu const, %zu sub -> IW8 Material(0x78)",
             pm.name.c_str(), pm.textures.size(), pm.constants.size(), pm.subMaterials.size());
}

} // namespace convert
