// Replay 1.20 structural map-family writer.
#include "../convert/registry.h"
#include "../common/log.h"
#include "../common/json.hpp"
#include "iw8_zone.h"
#include "iw8_structs.h"
#include "maps_write.h"
#include "replay_render.h"
#include "../zonesrc/zone_source.h"
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace iw8maps {

using iw8::ZoneWriter;
using namespace iw8; // XFILE_BLOCK_*, PTR_*

// stamp helpers into a fixed-size zeroed struct buffer
static inline void stamp64(uint8_t* p, size_t off, uint64_t v) { std::memcpy(p + off, &v, 8); }
static inline void stamp32(uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, 4); }
static inline void stamp16(uint8_t* p, size_t off, uint16_t v) { std::memcpy(p + off, &v, 2); }
static inline void stampf (uint8_t* p, size_t off, float v)    { std::memcpy(p + off, &v, 4); }

void emitMapEntsBody(ZoneWriter& zw, const char* assetName, const std::string& ents) {
    std::vector<uint8_t> me(kSizeMapEnts, 0);
    stamp64(me.data(), kME_name, PTR_FOLLOWS);
    stamp64(me.data(), kME_entityString, PTR_FOLLOWS);
    stamp32(me.data(), kME_numEntityChars, (uint32_t)(ents.size() + 1)); // strlen + NUL
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);   // struct -> retail STREAM 1 (live: map_ents curStream=1; map family != gfx/glass which use stream 2). [unused ZoneWriter path; kept correct]
    zw.write(me.data(), me.size());
        zw.pushStream(XFILE_BLOCK_VIRTUAL);
        zw.writeStr(assetName);                              // name
        zw.write(ents.data(), ents.size()); zw.writeT<uint8_t>(0); // entityString + NUL
        zw.popStream();
    zw.popStream();
}

void emitColMapBody(ZoneWriter& zw, const char* assetName, const std::string& ents) {
    std::vector<uint8_t> cm(kSizeClipMap, 0);
    stamp64(cm.data(), kCM_name, PTR_FOLLOWS);
    stamp32(cm.data(), kCM_isInUse, 1);
    stamp64(cm.data(), kCM_mapEnts, PTR_FOLLOWS);     // follows inline (duplicate MapEnts after clipMap)
    // generous world AABB (broadphase only; covers the spawns)
    stampf(cm.data(), kCM_bpMin + 0, -100000.f); stampf(cm.data(), kCM_bpMin + 4, -100000.f); stampf(cm.data(), kCM_bpMin + 8, -100000.f);
    stampf(cm.data(), kCM_bpMax + 0,  100000.f); stampf(cm.data(), kCM_bpMax + 4,  100000.f); stampf(cm.data(), kCM_bpMax + 8,  100000.f);
    stamp32(cm.data(), kCM_havokSize, 0);
    stamp64(cm.data(), kCM_havokData, PTR_NULL);      // no collision data; requires the selected-prototype no-shapes path
    stamp32(cm.data(), kCM_checksum, 0);

    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);   // struct -> retail STREAM 1 (live: map_ents curStream=1; map family != gfx/glass which use stream 2). [unused ZoneWriter path; kept correct]
    zw.write(cm.data(), cm.size());
        zw.pushStream(XFILE_BLOCK_VIRTUAL);
        zw.writeStr(assetName);                     // clipMap name
        zw.popStream();
        // mapEnts = -2 => the loader reads a MapEnts inline right here (the byte-safe duplicate)
        emitMapEntsBody(zw, assetName, ents);
    zw.popStream();
}

void emitComMapBody(ZoneWriter& zw, const char* assetName) {
    std::vector<uint8_t> cw(kSizeComWorld, 0);
    stamp64(cw.data(), kCW_name, PTR_FOLLOWS);
    stamp32(cw.data(), kCW_isInUse, 1);
    // primaryLightCount/umbraGates/etc. stay 0 (valid-empty)
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);   // struct -> retail STREAM 1 (live: map_ents curStream=1; map family != gfx/glass which use stream 2). [unused ZoneWriter path; kept correct]
    zw.write(cw.data(), cw.size());
        zw.pushStream(XFILE_BLOCK_VIRTUAL);
        zw.writeStr(assetName);                     // name
        zw.popStream();
    zw.popStream();
}

void emitGlassMapBody(ZoneWriter& zw, const char* assetName) {
    std::vector<uint8_t> gl(kSizeGlass, 0);
    stamp64(gl.data(), kGL_name, PTR_FOLLOWS);
    stamp64(gl.data(), kGL_glassData, PTR_FOLLOWS);
    // Replay Load_GlassWorldPtr uses stream 1 for the body.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);
    zw.write(gl.data(), gl.size());
        zw.pushStream(XFILE_BLOCK_VIRTUAL);
        zw.writeStr(assetName);                     // name
        // Replay Load_GlassWorld DF31E0 calls AllocLoad_G_GlassData DFC9E0
        // (alignment 7), then Load_G_GlassData DFD220 (40 bytes). G_InitGlass
        // 1213500 always dereferences this object even with no breakable pieces.
        zw.align(7);
        const uint8_t glassData[40]{};
        zw.write(glassData, sizeof(glassData));
        zw.popStream();
    zw.popStream();
    // Preload/Postload use stream 2 (Load uses stream 1) for the same body.
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD); zw.align(7);
    zw.reserveCalc(kSizeGlass); zw.align(31); zw.popStream();
}

// One-cell Replay render world with optional converted BSP triangles and a sun.
void emitGfxMapBody(ZoneWriter& zw, const char* assetName, const std::string& meshPath) {
    const auto mesh=replayrender::Load(meshPath);
    std::vector<uint8_t> gw(kSizeGfxWorld, 0);
    stamp64(gw.data(), kGW_name, PTR_FOLLOWS);
    stamp64(gw.data(), kGW_baseName, PTR_FOLLOWS);
    stamp32(gw.data(), kGW_bspVersion, 243);
    stamp32(gw.data(), 0x14, 1); // lastSunPrimaryLightIndex
    stamp32(gw.data(), 0x18, 2); // reserved zero plus authored sun, matching ComWorld
    stamp64(gw.data(), 0x3D68, PTR_FOLLOWS); // runtime GfxLight[2], zero-fill stream 4
    // Renderer material sorting always reads models[0].surfaceCount (+0x58),
    // even when the map has no surfaces. Native Load_GfxBrushModelArray DD1750
    // consumes 96 bytes per model; allocator DCF750 uses alignment mask 3.
    stamp32(gw.data(), 0x3DF0, 1);
    stamp64(gw.data(), 0x3DF8, PTR_FOLLOWS);
    // bounds @0x78 = midPoint(0,0,0) + halfSize(100000^3): one huge AABB covering all spawns/geometry.
    stampf(gw.data(), kGW_bounds + 0,  0.f); stampf(gw.data(), kGW_bounds + 4,  0.f); stampf(gw.data(), kGW_bounds + 8,  0.f);
    stampf(gw.data(), kGW_bounds + 12, 100000.f); stampf(gw.data(), kGW_bounds + 16, 100000.f); stampf(gw.data(), kGW_bounds + 20, 100000.f);
    // dpvsPlanes @0x90: 1 cell, 1 leaf node, no planes/sceneEntCellBits.
    stamp32(gw.data(), kGW_dpvsPlanes + kGW_dpp_cellCount,  1);
    stamp16(gw.data(), kGW_dpvsPlanes + kGW_dpp_planeCount, 0);
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_planes,     PTR_NULL);
    stamp16(gw.data(), kGW_dpvsPlanes + kGW_dpp_nodeCount,  1);
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_nodes,      PTR_FOLLOWS); // REQUIRED non-null
    // Load_GfxWorldDpvsPlanes D94FB0 allocates 512 uints per cell in stream 4.
    stamp64(gw.data(), kGW_dpvsPlanes + kGW_dpp_sceneEntCellBits, PTR_FOLLOWS);
    stamp64(gw.data(), kGW_cells,             PTR_FOLLOWS);              // REQUIRED non-null
    stamp64(gw.data(), kGW_cellTransientInfos, PTR_FOLLOWS);
    // Replay draw @0x648: transientZoneCount @+0x184, pointers @+0x188.
    // Zone zero is resident and required by R_ReflectionProbe_WorldStartup.
    stamp32(gw.data(), 0x648 + 0x184, 1);
    stamp64(gw.data(), 0x648 + 0x188, PTR_FOLLOWS);
    stamp64(gw.data(),0x648+0xF0,PTR_FOLLOWS); // mandatory IES lookup image
    stamp64(gw.data(), kGW_cellVisBits,       PTR_FOLLOWS);             // REQUIRED non-null (cellCount-gated memset, no own count)
    stamp64(gw.data(), 0x3EF8, PTR_FOLLOWS); // cellHasSunLitSurfsBits[1]
    stamp32(gw.data(), 0x3F98 + 8, 1); // primary-light visibility word count
    stamp64(gw.data(), 0x41C0, PTR_FOLLOWS); // primaryLightVisData[1]
    // R_EntityMoved 1959A60 indexes localClient*80 + entityNum/32.
    stamp32(gw.data(), 0x3F20, 160);
    stamp64(gw.data(), 0x3F28, PTR_FOLLOWS);
    replayrender::StampWorld(gw,mesh);
    // Replay Load_GfxWorldPtr RVA 0xD96DA0: stream 1, alignment mask 15.
    // Unimplemented render data remains null; do not stamp pointers from another build.
    zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(15);
    zw.write(gw.data(), gw.size());
        zw.pushStream(XFILE_BLOCK_VIRTUAL);              // dev DB_PushStreamPos(8) -> retail VIRTUAL/5
        zw.writeStr(assetName);                              // name     = raw "maps/mp/mp_test.d3dbsp\0" (struct stamp = PTR_FOLLOWS)
        zw.writeStr(assetName);                              // baseName = raw "maps/mp/mp_test.d3dbsp\0" (own copy; struct stamp = PTR_FOLLOWS)
        // dpvsPlanes sub-arrays (planeCount=0 -> no planes; sceneEntCellBits NULL -> skip):
        { zw.align(1); uint16_t node = 0x0001; zw.write(&node, sizeof(node)); }   // nodes[1] (single leaf)
        zw.align(1); zw.writeT<uint32_t>(0); // cell zero -> transient zone zero, cell zero
        // cells: align 8, one GfxCell covering the world (portalCount=0 -> per-cell portal walk skipped).
        zw.align(7);
        {
            std::vector<uint8_t> cell(kSizeGfxCell, 0);
            stampf(cell.data(), 0,  0.f); stampf(cell.data(), 4,  0.f); stampf(cell.data(), 8,  0.f);
            stampf(cell.data(), 12, 100000.f); stampf(cell.data(), 16, 100000.f); stampf(cell.data(), 20, 100000.f);
            stamp32(cell.data(), kGC_portalCount, 0);
            stamp64(cell.data(), kGC_portals, PTR_NULL);
            zw.write(cell.data(), cell.size());
        }
        replayrender::EmitSurfaces(zw,mesh);
        // Load_GfxWorldDraw D96710 loads iesLookupTexture before transient zones.
        // With no authored local lights, native white gives a neutral IES lookup.
        // Reference the existing image; no pixels or stock image data are copied.
        zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD);zw.align(15);
        uint8_t iesImage[0xE8]{};stamp64(iesImage,0,PTR_FOLLOWS);
        zw.write(iesImage,sizeof(iesImage));
        zw.pushStream(XFILE_BLOCK_VIRTUAL);zw.writeStr(",$white");zw.popStream();
        zw.popStream();
        // Load_GfxWorldDraw D96710 visits its 1536 inline asset pointers.
        // Load_GfxWorldTransientZonePtr D970B0: stream 1, alignment 7;
        // Load_GfxWorldTransientZone D96E90: 0x148 bytes, name in VIRTUAL.
        zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(7);
        uint8_t transient[0x148]{};
        stamp64(transient, 0, PTR_FOLLOWS);
        stamp32(transient, 0xD8, 1); // one resident draw cell
        stamp64(transient, 0xE0, PTR_FOLLOWS); // aabbTreeCounts
        stamp64(transient, 0xE8, PTR_FOLLOWS); // GfxCellTree[1]
        replayrender::StampTransient(transient,mesh);
        zw.write(transient, sizeof(transient));
        zw.pushStream(XFILE_BLOCK_VIRTUAL); zw.writeStr(assetName);
        replayrender::EmitVertices(zw,mesh);
        zw.align(3); zw.writeT<uint32_t>(1);
        zw.align(7); zw.writeT<uint64_t>(PTR_FOLLOWS);
        zw.align(7);
        // Load_GfxCellTree D908E0 reads a separate tree count via
        // Load_StreamAlloc 11B29B0 (four bytes in stream 1).
        zw.pushStream(XFILE_BLOCK_TEMP_PRELOAD); zw.align(3); zw.writeT<uint32_t>(1); zw.popStream();
        uint8_t tree[48]{};
        stampf(tree,12,100000.f);stampf(tree,16,100000.f);stampf(tree,20,100000.f);
        stamp32(tree,24,mesh.count);
        zw.write(tree,sizeof(tree));
        zw.popStream();
        zw.popStream();
        zw.align(3);
        uint8_t worldModel[96]{};
        stamp32(worldModel,0x58,mesh.count);
        zw.write(worldModel, sizeof(worldModel));
        replayrender::EmitSortedSurfaces(zw,mesh);
        zw.popStream();
    zw.popStream();

    // Replay Load_GfxWorld allocates and zero-fills cellVisBits in stream 4.
    // One cell needs four bytes. Reserve alignment slack, with no disk payload.
    // Preload_GfxWorldPtr RVA 0xDB2EB0 and Postload RVA 0xDA6CD0 use stream 2.
    // Reserve both body destinations; only one copy is serialized.
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD); zw.align(15);
    zw.reserveCalc(kSizeGfxWorld); zw.align(31); zw.popStream();
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD); zw.align(7);
    zw.reserveCalc(0x148); zw.align(31); zw.popStream();
    zw.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);zw.align(15);zw.reserveCalc(0xE8);zw.align(31);zw.popStream();
    zw.pushStream(XFILE_BLOCK_SHARED_STREAM);   // Replay stream 4 = zero-fill
        zw.reserveCalc(0xD00); // scene/entity motion bits, light zero, visibility, alignment
    zw.popStream();
}

} // namespace iw8maps

namespace convert {

using nlohmann::json;

namespace {
    // Per-ZoneWriter de-dup of the map family (idempotence across the 3 dispatch rows). Keyed by the
    // ZoneWriter pointer + asset name. A static set is fine: one build runs one ZoneWriter at a time.
    std::set<std::string> g_emittedMaps;

    std::string guardKey(const iw8::ZoneWriter& zw, const std::string& name) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%p:", (const void*)&zw);
        return std::string(buf) + name;
    }
}

void iw8_write_maps(iw8::ZoneWriter& zw, iw3sr::ZoneSource& zs, const char* name) {
    const std::string assetName = name ? name : ("maps/mp/" + zs.map() + ".d3dbsp");

    // idempotence: only register the map family once per (ZoneWriter, assetName).
    std::string key = guardKey(zw, assetName);
    if (g_emittedMaps.count(key)) {
        zt::debug("iw8 maps: family for '%s' already registered — skip", assetName.c_str());
        return;
    }
    g_emittedMaps.insert(key);

    // Pull the converted IW8 entityString from the zone-source (Stage A wrote it). Fall back to a
    // minimal worldspawn so the map is structurally non-empty if the file is missing.
    std::string ents;
    if (!zs.getMapSubText("entityString", ents) || ents.empty()) {
        zt::warn("iw8 maps: no entityString in zone-source — using minimal worldspawn");
        ents = "{ 212 \"worldspawn\" }\n";
    }

    // Register the map family on the ZoneWriter. Order = load order within the zone (the driver frames
    // the XAsset[] array then invokes the bodies in this registration order).
    //   map_ents(29), col_map(23), com_map(24), glass_map(25), gfx_map(31).
    // map_ents + col_map are the load-critical pair proven by srv_mp_test.ff; com/glass/gfx valid-empty.
    // CAPTURE BY VALUE: the bodies run later (during zw.build()), after this function returns, so the
    // assetName/ents strings must be owned by each lambda (a const char* into a local would dangle).
    zw.add(ASSET_TYPE_MAP_ENTS, assetName, [assetName, ents](iw8::ZoneWriter& w) {
        iw8maps::emitMapEntsBody(w, assetName.c_str(), ents);
    });
    zw.add(ASSET_TYPE_COL_MAP, assetName, [assetName, ents](iw8::ZoneWriter& w) {
        iw8maps::emitColMapBody(w, assetName.c_str(), ents);
    });
    zw.add(ASSET_TYPE_COM_MAP, assetName, [assetName](iw8::ZoneWriter& w) {
        iw8maps::emitComMapBody(w, assetName.c_str());
    });
    zw.add(ASSET_TYPE_GLASS_MAP, assetName, [assetName](iw8::ZoneWriter& w) {
        iw8maps::emitGlassMapBody(w, assetName.c_str());
    });
    zw.add(ASSET_TYPE_GFX_MAP, assetName, [assetName](iw8::ZoneWriter& w) {
        iw8maps::emitGfxMapBody(w, assetName.c_str());
    });

    zt::info("iw8 maps: registered map_ents+col_map+com_map+glass_map+gfx_map for '%s' (ents=%zu bytes)",
             assetName.c_str(), ents.size());
}

} // namespace convert
