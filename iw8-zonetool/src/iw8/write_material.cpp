// write_material.cpp — DUMP-PATH IW8 Material(11)=0x78 serializer (Tier2, FLAG-GATED via --assets).
// Serializes a converted IW8 material (convdump::mtl::Iw8Material) into the ZoneWriter, following the
// load-read-order pointer-stamping convention (ZONE_LOAD_MODEL.md). Self-contained under convdump::mtl;
// does NOT collide with the legacy IW3-.ff writer (iw8mtl / convert::iw8_write_material).
//
// LOAD-READ-ORDER: zw.add() frames the XAsset (header=-3 insert) and runs the body with the current
// stream = VIRTUAL(8). Inside the body we:
//   1. write the 0x78 Material struct with each pointer field stamped — -2 (PTR_FOLLOWS) for the payloads
//      we actually emit, 0 (PTR_NULL) for the rest;
//   2. emit the followed payloads in STRUCT-FIELD ORDER (the order the loader walks the pointer fields,
//      top-to-bottom): name(0x00) -> textureTable(0x48) -> constantTable(0x50) -> subMaterials(0x70).
//      packedAtlasData(0x38)/techniqueSet(0x40)/decalVolumeMaterial(0x58)/constantBufferIndex(0x60)/
//      constantBufferTable(0x68) are NULL (techset DEFERRED; no atlas/decal/cbuffer in this prototype).
//
// IMAGE BINDING: MaterialTextureDef.image is a cross-asset GfxImage*. Offline, without the integrator's
// cross-zone asset-pointer table, it is stamped NULL (load-safe: the loader leaves it null, engine binds
// its default texture). textureCount + the table size are still correct, so the struct is valid; the
// bound NAME is preserved in Iw8TexBinding for a later link pass. (Matches the legacy path's decision.)
//
// SENTINEL DISCIPLINE (the task's correctness bar): -2 follows / -3 insert / -1 shared / 0 null, RAW (no
// mask). A pointer field is -2 IFF its payload is emitted right after, else 0. Counts in the struct MUST
// match the number of elements emitted (an over/under-walk = a load-time crash).
#include "dumpsrc/material_dumpsrc.h"
#include "iw8/iw8_zone.h"
#include "common/log.h"
#include <cstdint>
#include <cstring>

using namespace iw8_focus;
using namespace zt;

namespace convdump::mtl {

bool writeMaterial(iw8::ZoneWriter& zw, const Iw8Material& im) {
    if (!im.ok) { err("write_material: converted material not ok (name='%s')", im.name.c_str()); return false; }

    // Register as a top-level material(11) asset. Capture `im` by value so the body owns its data.
    zw.add(ASSET_TYPE_MATERIAL, im.name, [im](iw8::ZoneWriter& w) {
        // ---- 1) the 0x78 Material struct with pointer fields stamped ----
        Material m = im.mat; // scalar fields already filled by convert()

        // Stamp pointer slots with RAW sentinels (host pointer fields are 8 bytes).
        m.name                = reinterpret_cast<const char*>(iw8::PTR_FOLLOWS);            // 0x00 follows
        m.packedAtlasData     = nullptr;                                                    // 0x38 null
        m.techniqueSet        = nullptr;                                                    // 0x40 null (techset deferred)
        m.textureTable        = im.textures.empty()  ? nullptr
                                  : reinterpret_cast<MaterialTextureDef*>(iw8::PTR_FOLLOWS);// 0x48 follows
        m.constantTable       = im.constants.empty() ? nullptr
                                  : reinterpret_cast<MaterialConstantDef*>(iw8::PTR_FOLLOWS);// 0x50 follows
        m.decalVolumeMaterial = nullptr;                                                    // 0x58 null
        m.constantBufferIndex = nullptr;                                                    // 0x60 null
        m.constantBufferTable = nullptr;                                                    // 0x68 null
        m.subMaterials        = im.subMaterials.empty() ? nullptr
                                  : reinterpret_cast<const char**>(iw8::PTR_FOLLOWS);       // 0x70 follows

        // Defensive: keep the //CNT fields exactly in step with the emitted table lengths.
        m.textureCount  = static_cast<uint8_t>(im.textures.size() & 0xFF);
        m.constantCount = static_cast<uint8_t>(im.constants.size() & 0xFF);
        m.layerCount    = static_cast<uint8_t>(im.subMaterials.size() & 0xFF);

        static_assert(sizeof(Material) == 0x78, "IW8 Material must be 0x78 (g_assetSizes[11])");
        // The 0x78 struct is read from TEMP_PRELOAD(1) at 8-align — dev.i64 Load_MaterialHandle does
        // PushStreamPos(1)+FixStreamAlignment(7) (NOT VIRTUAL/8; fixed per the workflow BLOCKER). Payloads
        // (name/tables/subMaterials) follow in VIRTUAL(8).
        w.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
        w.align(7);
        w.write(&m, sizeof(m));

        // ---- 2) followed payloads in STRUCT-FIELD ORDER -> VIRTUAL(8) ----
        w.pushStream(iw8::XFILE_BLOCK_VIRTUAL);

        // name (0x00) — XString
        w.writeStr(im.name.c_str());

        // textureTable (0x48): MaterialTextureDef[textureCount], 8-aligned (loader FixStreamAlignment(7)).
        if (!im.textures.empty()) {
            w.align(7);
            for (const auto& tb : im.textures) {
                MaterialTextureDef td{};
                std::memset(&td, 0, sizeof(td));
                td.index = tb.index;                 // 0x00 sampler/dest slot
                td.image = nullptr;                  // 0x08 cross-asset GfxImage* -> NULL (load-safe)
                static_assert(sizeof(MaterialTextureDef) == 0x10, "MaterialTextureDef must be 0x10");
                w.write(&td, sizeof(td));
            }
        }

        // constantTable (0x50): MaterialConstantDef[constantCount], stride 0x14, 16-aligned — dev.i64
        // Load_Material uses FixStreamAlignment(0xF) here (NOT 4; fixed per the workflow MEDIUM finding).
        if (!im.constants.empty()) {
            w.align(15);
            for (const auto& c : im.constants) {
                static_assert(sizeof(MaterialConstantDef) == 0x14, "MaterialConstantDef stride 0x14");
                w.write(&c, sizeof(c)); // all-scalar; no pointer to stamp
            }
        }

        // subMaterials (0x70): const char*[layerCount] (each element -2 follows) then each string,
        // 8-aligned. (None for the IW5 dump; emitted for completeness / future multi-layer materials.)
        if (!im.subMaterials.empty()) {
            w.align(7);
            for (size_t i = 0; i < im.subMaterials.size(); ++i) w.writeFollows();
            for (const auto& s : im.subMaterials) w.writeStr(s.c_str());
        }

        w.popStream(); // VIRTUAL(8)
        w.popStream(); // TEMP_PRELOAD(1)
    });

    info("write_material '%s': %zu tex, %zu const, %zu sub -> IW8 Material(0x78)",
         im.name.c_str(), im.textures.size(), im.constants.size(), im.subMaterials.size());
    return true;
}

// One-shot read -> convert -> register-for-write (the integrator's --assets entry point per material).
bool emitMaterialFromDump(iw8::ZoneWriter& zw, const std::string& filePath, const std::string& displayName) {
    DumpMaterial d = readMaterialJson(filePath, displayName);
    if (!d.loaded) return false;
    Iw8Material im = convert(d);
    if (!im.ok) { err("emitMaterialFromDump: convert failed for '%s'", filePath.c_str()); return false; }
    return writeMaterial(zw, im);
}

} // namespace convdump::mtl
