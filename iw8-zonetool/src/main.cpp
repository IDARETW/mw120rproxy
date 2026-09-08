// main.cpp — iw8-zonetool CLI (SPEC §0).
//   fromdump <dump_dir> <map> [-o <out_dir>]     CoD4 ZoneTool dump -> five IW8 zone files
//   validate-package <dir> <map>                  static package validation
//   inspect <file.ff>                             print a header
// Global flags: --oodle <path>, -v (verbose), -q (quiet).
//
// This prototype reads a CoD4 ZoneTool dump folder. It writes the five required zone files and a
// MW120 Replay package manifest. The output has structural checks. It needs a live 1.20 test before
// it can claim that the game accepts its layout, auth data, collision, or render data.
#include "common/ff_io.h"
#include "common/fs_util.h"
#include "common/log.h"
#include "common/oodle.h"
#include "iw3/iw3_zone.h"
#include "iw8/iw8_zone.h"
#include "iw8/map_zone.h"
#include "iw8/maps_write.h"
#include "iw8/replay_render.h"
#include "iw8/write_xsurface.h"       // iw8xs_dump::writeXModelSurfsFromDump (Tier2)
#include "iw8/iw8_ffheader.h"
#include "zonesrc/zone_source.h"
#include "dumpsrc/dump_source.h"
#include "dumpsrc/material_dumpsrc.h" // convdump::mtl::emitMaterialFromDump (Tier2)
#include "dumpsrc/image_dump.h"       // dumpimg:: read/convert/addImageAsset (Tier2)
#include "dumpsrc/xmodel_dump.h"      // iw8::addXModelFromDump (Tier2)
#include "convert/registry.h"
#include "convert/maps_convert.h"
#include <cstdio>
#include <cctype>
#include <cstring>
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>
#include "common/json.hpp"

using namespace zt;

namespace {

struct Args {
    std::string cmd;
    std::vector<std::string> pos;     // positional args after cmd
    std::string outDir;               // -o
    std::string oodlePath;            // --oodle
    Iw8Codec codec = Iw8Codec::Stored;
    bool        assets = false;       // --assets : emit Tier2 (material/image/xmodel/xmodelsurfs) in main zone
    std::string dumpDir;              // fromdump source (threaded to the Tier2 main-zone builder)
};

void usage() {
    std::printf(
        "iw8-zonetool — offline IW3->IW8 fastfile converter\n"
        "usage:\n"
        "  iw8-zonetool fromdump <dumpDir> <map>   [-o <out_dir>]   (ZoneTool dump -> five IW8 .ff)\n"
        "  iw8-zonetool inspect  <file.ff>\n"
        "  iw8-zonetool validate-package <package_dir> <map>\n"
        "flags: --assets (emit Tier2 material/image/xmodel/xmodelsurfs in the main zone; default OFF)\n"
        "       --stored (Replay native IWC framing, default)  -v  -q\n");
}

bool parse(int argc, char** argv, Args& a) {
    if (argc < 2) return false;
    a.cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-o" && i + 1 < argc)            a.outDir = argv[++i];
        else if (s == "--oodle" && i + 1 < argc)  a.oodlePath = argv[++i];
        else if (s == "--stored") a.codec = Iw8Codec::Stored;
        else if (s == "--assets")                 a.assets = true;
        else if (s == "-v")                       zt::g_logLevel = 3;
        else if (s == "-q")                       zt::g_logLevel = 0;
        else                                      a.pos.push_back(s);
    }
    return true;
}

bool isMapId(const std::string& map)
{
    if (map.size() < 4 || map.size() > 63 || map.rfind("mp_", 0) != 0) return false;
    for (const unsigned char ch : map)
    {
        if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') return false;
    }
    return true;
}

bool requireMapId(const std::string& map)
{
    if (isMapId(map)) return true;
    zt::err("map id '%s' is invalid; use lower-case mp_<name>", map.c_str());
    return false;
}

std::array<std::string, 5> zoneNames(const std::string& map)
{
    return {map, "srv_" + map, "eng_" + map, "ww_" + map, "techsets_" + map};
}

bool validateZoneFile(const std::string& path)
{
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes))
    {
        zt::err("validate: cannot read %s", path.c_str());
        return false;
    }
    if (bytes.size() < 0x8C)
    {
        zt::err("validate: %s has no complete IW8 header", path.c_str());
        return false;
    }

    IW8_DB_FFHeader header{};
    std::memcpy(&header, bytes.data(), (std::min)(bytes.size(), sizeof(header)));
    const bool stored = std::memcmp(header.magic, "IWffc100", 8) == 0;
    if (!stored && std::memcmp(header.magic, iw8ff::kMagic, 8) != 0)
    {
        zt::err("validate: %s has unsupported fastfile magic", path.c_str());
        return false;
    }
    if (header.headerVersion != iw8ff::kHeaderVersion || header.xfileVersion != iw8ff::kXFileVersion)
    {
        zt::err("validate: %s has an unsupported header version", path.c_str());
        return false;
    }
    if (header.dashCompressBuild != (stored ? 0 : 1) || header.dashEncryptBuild != 0)
    {
        zt::err("validate: %s has an unsupported compression/encryption mode", path.c_str());
        return false;
    }
    if (stored ? (std::memcmp(bytes.data()+0x88, "\x01IWC", 4) != 0 || header.xfileHeader.size != bytes.size()-0x8C)
               : (bytes.size() < sizeof(header) || std::memcmp(header.xfileHeader.encryption.magic, iw8ff::kSigMagic, 8) != 0))
    {
        zt::err("validate: %s has invalid resident frame marker or stored length", path.c_str());
        return false;
    }
    if (header.residentPartSize != bytes.size() - iw8ff::kResidentBase ||
        header.xfileHeader.size == 0 || header.alwaysLoadedPartSize == 0)
    {
        zt::err("validate: %s has invalid resident framing", path.c_str());
        return false;
    }
    uint64_t streamTotal = 0;
    for (uint64_t size : header.xfileHeader.blockSize) streamTotal += size;
    if (streamTotal < header.xfileHeader.size)
    {
        zt::err("validate: %s has invalid stream sizes", path.c_str());
        return false;
    }
    return true;
}

bool validatePackage(const std::string& packageDir, const std::string& map)
{
    if (!requireMapId(map)) return false;
    bool ok = true;
    for (const std::string& zone : zoneNames(map))
    {
        ok = validateZoneFile(path_join(packageDir, zone + ".ff")) && ok;
    }
    if (ok) zt::info("validate: package '%s' passes structural checks", map.c_str());
    return ok;
}

bool writeStarterManifest(const std::string& outDir, const std::string& map)
{
    const std::string content =
        "{\n"
        "  \"schema\": 1,\n"
        "  \"id\": \"" + map + "\",\n"
        "  \"title\": \"" + map + "\",\n"
        "  \"description\": \"CoD4 graybox conversion prototype\",\n"
        "  \"layout\": \"replay-1.20-bsp-v11\",\n"
        "  \"gametypes\": [\"tdm\"]\n"
        "}\n";
    const std::string path = path_join(outDir, "manifest.json");
    // Rebuilding fastfiles must not erase a package's UI/dependency metadata.
    if (file_exists(path)) return true;
    if (!write_file_str(path, content))
    {
        zt::err("build: cannot write %s", path.c_str());
        return false;
    }
    return true;
}

// Default IW8 entityString for the srv map zone when the zone-source has none yet (skeleton). A single
// info_player_deathmatch keeps the map structurally non-empty.
const char* kDefaultEnts =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "}\n"
    "{\n"
    "\"origin\" \"0 0 64\"\n"
    "\"classname\" \"info_player_deathmatch\"\n"
    "}\n";

int cmd_read(const Args& a) {
    (void)a;
    zt::err("read: raw IW3 fastfile input is not supported; use a CoD4 ZoneTool dump folder");
    return 2;
}

// ---- Tier2 main-zone assembly (--assets) --------------------------------------------------------
// Populate the main/client zone's ZoneWriter with the dumped player/visual assets. ALL of this is
// flag-gated (--assets) and runs on a SEPARATE ZoneWriter from the Tier1 srv zone, so it can never
// touch the guaranteed-loadable map zone. On any per-asset failure we log + skip (Tier2 must never
// abort the build). `zw` already has gfx_map+glass_map registered; we append the dumped families.
//   material(11) : materials/<2char>/<name> (JSON)  -> convdump::mtl::emitMaterialFromDump
//   image(19)    : images/<stem>.ffImg              -> dumpimg::read/convert/addImageAsset
//   xmodel(9)    : XModel/<name>.xme6               -> iw8::addXModelFromDump
//   xmodelsurfs(8): XSurface/<name>.xse             -> iw8xs_dump::writeXModelSurfsFromDump
// Returns a tally string for the build log; never throws.
struct Tier2Tally { int materials=0, images=0, xmodels=0, xsurfs=0; int matFail=0, imgFail=0, xmFail=0, xsFail=0; };

Tier2Tally addTier2Assets(iw8::ZoneWriter& zw, const std::string& dumpDir, const dumpsrc::DumpSource& ds) {
    Tier2Tally t;

    // --- material(11) ---
    for (const auto& rel : ds.listMaterials()) {  // rel = "<2char>/<name>"
        std::string filePath = path_join(path_join(dumpDir, "materials"), rel);
        if (convdump::mtl::emitMaterialFromDump(zw, filePath, /*displayName=*/rel)) ++t.materials;
        else ++t.matFail;
    }

    // --- image(19) ---
    for (const auto& stem : dumpimg::listImageDumps(dumpDir)) {
        dumpimg::ImageDumpFile in = dumpimg::readImageDump(dumpDir, stem);
        if (!in.loaded) { ++t.imgFail; continue; }
        dumpimg::Iw8ImageDef def = dumpimg::convertImage(in);
        if (dumpimg::addImageAsset(zw, def)) ++t.images;
        else ++t.imgFail;
    }

    // --- xmodel(9) ---
    for (const auto& name : ds.listXModels()) {
        if (iw8::addXModelFromDump(zw, dumpDir, name)) ++t.xmodels;
        else ++t.xmFail;
    }

    // --- xmodelsurfs(8) : enumerate the .xse files directly (one per LOD) ---
    {
        std::vector<std::string> xseFiles;
        if (list_dir(path_join(dumpDir, "XSurface"), xseFiles)) {
            for (const auto& f : xseFiles) {
                if (f.size() <= 4 || f.compare(f.size() - 4, 4, ".xse") != 0) continue;
                std::string surfName = f.substr(0, f.size() - 4); // "zonetool_<model>_<lod>"
                if (iw8xs_dump::writeXModelSurfsFromDump(zw, dumpDir, surfName)) ++t.xsurfs;
                else ++t.xsFail;
            }
        }
    }

    zt::info("build(--assets): Tier2 registered material=%d image=%d xmodel=%d xmodelsurfs=%d "
             "(skipped: mtl=%d img=%d xm=%d xs=%d)",
             t.materials, t.images, t.xmodels, t.xsurfs, t.matFail, t.imgFail, t.xmFail, t.xsFail);
    return t;
}

// ---- shared package emitter ----------------------------------------------------------------------
// Tier1 split (SPEC §4d): srv = map_ents(29)+col_map(23)+com_map(24) (the LOAD goal — ALWAYS, on its
// own ZoneBuffer). main(mp_test.ff) = gfx_map(31)+glass_map(25) valid-empty, PLUS (only with --assets)
// material+image+xmodel+xmodelsurfs from the dump. eng/ww/techsets are valid-empty. `bounds` (when
// valid) tightens the col_map broadphase AABB. `dumpDir` is empty for the legacy build path.
// Replay custom packages use IWffc100 + mode-1 IWC stored framing with the selected-package reset hook.
int writeMapPackage(const Args& a, const std::string& map, const std::string& outDir,
                    const std::string& ents, const iw8::MapBounds& bounds,
                    const std::string& dumpDir = "") {
    if (!requireMapId(map)) return 2;
    if (a.codec != Iw8Codec::Stored)
    {
        zt::err("build: Replay custom packages require --stored; legacy output has no valid signed auth framing");
        return 2;
    }
    mkdirs(outDir);

    auto paramsFromBuffer = [&](iw8::ZoneBuffer& zb) {
        Iw8WriteParams p;
        p.codec = a.codec;
        p.oodlePath = a.oodlePath;
        p.totalDecompressed = 0;
        for (int i = 0; i < iw8::IW8_MAX_XFILE_COUNT; ++i) {
            p.blockSize[i] = zb.streamSize(i);
            p.totalDecompressed += zb.streamSize(i);
        }
        p.calcSize = zb.calcSize();   // stream-4 reserveCalc bytes (in blockSize[] but not in body)
        return p;
    };
    auto paramsFromWriter = [&](iw8::ZoneWriter& zw) {
        Iw8WriteParams p;
        p.codec = a.codec;
        p.oodlePath = a.oodlePath;
        p.totalDecompressed = 0;
        for (int i = 0; i < iw8::IW8_MAX_XFILE_COUNT; ++i) {
            p.blockSize[i] = zw.blockSize(i);
            p.totalDecompressed += zw.blockSize(i);
        }
        p.calcSize = zw.calcSize();   // stream-4 reserveCalc bytes (in blockSize[] but not in body)
        return p;
    };

    std::string assetName = "maps/mp/" + map + ".d3dbsp";
    int rc = 0;

    // srv_<map>.ff : map_ents(29) + col_map(23) + com_map(24) — the guaranteed-loadable Tier1 zone.
    // (Its own ZoneBuffer; never sees a Tier2 asset, so --assets cannot affect it.)
    {
        iw8::ZoneBuffer zb;
        iw8::MapSun lighting;
        const auto lightPath=path_join(dumpDir,assetName+".lighting.json");
        if(file_exists(lightPath)){
            std::ifstream input(lightPath);const auto j=nlohmann::json::parse(input);
            if(j.at("schema")!=1)throw std::runtime_error("Invalid map lighting schema");
            lighting.intensity=j.at("intensity").get<float>();
            if(!std::isfinite(lighting.intensity)||lighting.intensity<0||lighting.intensity>4)throw std::runtime_error("Invalid sun intensity");
            float length=0,upLength=0,dot=0;
            for(unsigned k=0;k<3;++k){lighting.color[k]=j.at("color").at(k).get<float>();
                lighting.direction[k]=j.at("direction").at(k).get<float>();lighting.up[k]=j.at("up").at(k).get<float>();
                if(!std::isfinite(lighting.color[k])||lighting.color[k]<0||lighting.color[k]>1)throw std::runtime_error("Invalid sun color");
                length+=lighting.direction[k]*lighting.direction[k];upLength+=lighting.up[k]*lighting.up[k];dot+=lighting.direction[k]*lighting.up[k];}
            if(!std::isfinite(length+upLength+dot)||std::abs(length-1)>.001f||std::abs(upLength-1)>.001f||std::abs(dot)>.001f)throw std::runtime_error("Invalid sun basis");
        }
        iw8::buildSrvMapZone(zb, assetName.c_str(), ents, bounds,lighting);
        Iw8WriteParams p = paramsFromBuffer(zb);
        std::string out = path_join(outDir, "srv_" + map + ".ff");
        if (!iw8_write(out, zb.data(), p)) rc = 1;
    }

    // mp_test.ff (main/client) : gfx_map(31) + glass_map(25) valid-empty, +Tier2 when --assets.
    {
        iw8::ZoneWriter zw;
        // Always-present, load-safe valid-empty map visuals (SPEC §4d). Register via the proven
        // iw8maps body emitters (capture assetName by value — bodies run during build()).
        const std::string meshCandidate = path_join(dumpDir, assetName + ".render.json");
        const std::string meshPath = !dumpDir.empty() && file_exists(meshCandidate) ? meshCandidate : "";
        // Native patch-memory assets have a two-entry stack. Define material
        // dependencies before the world; world surfaces carry references only.
        replayrender::RegisterMaterial(zw,meshPath);
        zw.add(ASSET_TYPE_GFX_MAP, assetName, [assetName,meshPath](iw8::ZoneWriter& w) {
            iw8maps::emitGfxMapBody(w, assetName.c_str(),meshPath);
        });
        zw.add(ASSET_TYPE_GLASS_MAP, assetName, [assetName](iw8::ZoneWriter& w) {
            iw8maps::emitGlassMapBody(w, assetName.c_str());
        });

        if (a.assets) {
            if (dumpDir.empty()) {
                zt::warn("build: --assets set but no dump source -> main zone = gfx_map+glass_map only");
            } else {
                dumpsrc::DumpSource ds(dumpDir, map);
                addTier2Assets(zw, dumpDir, ds);
            }
        }

        zw.build();
        Iw8WriteParams p = paramsFromWriter(zw);
        std::string out = path_join(outDir, map + ".ff");
        if (!iw8_write(out, zw.body(), p)) rc = 1;
        zt::info("build: main zone '%s.ff' = %zu assets (gfx_map+glass_map%s)",
                 map.c_str(), zw.assetCount(), a.assets ? "+Tier2" : "+native render dependencies");
    }

    // eng_<map>.ff, ww_<map>.ff, techsets_<map>.ff are required valid-empty companions. The exact
    // MW120 loader order stays under target-build verification. The package requires all three names.
    auto writeEmpty = [&](const std::string& fname) {
        iw8::ZoneBuffer zb;
        iw8::buildEmptyZone(zb);
        Iw8WriteParams p = paramsFromBuffer(zb);
        std::string out = path_join(outDir, fname);
        if (!iw8_write(out, zb.data(), p)) rc = 1;
    };
    writeEmpty("eng_" + map + ".ff");
    writeEmpty("ww_" + map + ".ff");
    writeEmpty("techsets_" + map + ".ff");

    if (rc == 0 && !writeStarterManifest(outDir, map)) rc = 1;
    if (rc == 0 && !validatePackage(outDir, map)) rc = 1;
    if (rc == 0)
        zt::info("build: wrote five zones to %s (srv=map_ents+col_map+com_map, "
                 "main=gfx_map+glass_map%s, eng/ww/techsets valid-empty)",
                 outDir.c_str(), a.assets ? "+material+image+xmodel+xmodelsurfs" : "");
    return rc;
}

// ---- Stage B ------------------------------------------------------------------------------------
int cmd_build(const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    std::string assetDir = a.pos[0], map = a.pos[1];
    if (!requireMapId(map)) return 2;
    std::string outDir = a.outDir.empty() ? assetDir : a.outDir;

    iw3sr::ZoneSource zs(assetDir, map, /*create=*/false);
    if (!zs.manifestRead())
        zt::warn("build: no manifest at %s (skeleton: building srv from default ents)", assetDir.c_str());

    // entityString for the srv zone (from zone-source, else default).
    std::string ents;
    if (!zs.getMapSubText("entityString", ents) || ents.empty()) ents = kDefaultEnts;

    iw8::MapBounds bounds; // invalid -> generous AABB
    return writeMapPackage(a, map, outDir, ents, bounds);
}

// ---- fromdump (NEW Stage-A source = the ZoneTool dump folder) -----------------------------------
// Reads the CoD4 ZoneTool dump at <dumpDir> and emits the five IW8 .ff files. Tier1: ents/colmap/comworld ->
// srv map zone. entityString source priority:
//   1) <dumpDir>/<map>_iw8_ents.txt (IW8 numeric-key input),
//   2) the dump's .ents, converted from CoD4 text keys to confirmed IW8 numeric keys,
//   3) the built-in default ents.
int cmd_fromdump(const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    std::string dumpDir = a.pos[0], map = a.pos[1];
    if (!requireMapId(map)) return 2;
    std::string outDir = a.outDir.empty() ? path_join(dumpDir, map + "_out") : a.outDir;

    dumpsrc::DumpSource ds(dumpDir, map);
    if (!ds.hasMapFiles()) {
        zt::err("fromdump: no maps/mp/%s.d3dbsp.* under %s", map.c_str(), dumpDir.c_str());
        return 1;
    }

    // --- entityString resolution (numeric-key file preferred) ---
    std::string ents;
    bool haveEnts = false;
    std::vector<std::string> entsCandidates = {
        path_join(dumpDir, map + "_iw8_ents.txt"),
        path_join(path_dir(dumpDir), map + "_iw8_ents.txt"),
    };
    for (const auto& c : entsCandidates) {
        if (file_exists(c) && read_file_str(c, ents) && !ents.empty()) {
            zt::info("fromdump: entityString = IW8 numeric-key file %s (%zu bytes)", c.c_str(), ents.size());
            haveEnts = true; break;
        }
    }
    dumpsrc::EntsDump ed = ds.loadEnts();
    if (!haveEnts) {
        if (ed.loaded && !ed.text.empty()) {
            const size_t converted = convert::iw3ToIw8EntityString(ed.text, ents);
            if (converted == 0) {
                zt::err("fromdump: could not convert a worldspawn or spawn entity from %s", dumpDir.c_str());
                return 1;
            }
            zt::info("fromdump: converted %zu CoD4 entities to IW8 numeric keys", converted);
            haveEnts = true;
        } else {
            ents = kDefaultEnts;
            zt::warn("fromdump: no ents available; using built-in default");
        }
    }

    // --- colmap -> name + broadphase bounds (tight from verts, else generous) ---
    dumpsrc::ClipMapDump cmd = ds.loadClipMap();
    iw8::MapBounds bounds;
    if (cmd.loaded) {
        zt::info("fromdump: colmap name='%s' mapEntsLink='%s' verts=%d boundsFromVerts=%d",
                 cmd.name.c_str(), cmd.mapEntsName.c_str(), cmd.numVerts, (int)cmd.boundsFromVerts);
        if (cmd.boundsFromVerts) {
            for (int k = 0; k < 3; ++k) { bounds.mn[k] = cmd.boundsMin[k]; bounds.mx[k] = cmd.boundsMax[k]; }
            bounds.valid = true;
            zt::info("fromdump: col_map broadphase AABB [%.1f %.1f %.1f]..[%.1f %.1f %.1f]",
                     bounds.mn[0], bounds.mn[1], bounds.mn[2], bounds.mx[0], bounds.mx[1], bounds.mx[2]);
        } else {
            zt::info("fromdump: col_map broadphase = generous ±100000 box (Tier1-safe fallback)");
        }
    } else {
        zt::warn("fromdump: colmap not parsed; generous AABB");
    }

    // --- comworld (informational; com_map emitted valid-empty regardless) ---
    dumpsrc::ComWorldDump cw = ds.loadComWorld();
    if (cw.loaded)
        zt::info("fromdump: comworld name='%s' isInUse=%d primaryLightCount=%d -> com_map valid-empty",
                 cw.name.c_str(), cw.isInUse, cw.primaryLightCount);

    // --- emit the five zones (pass dumpDir so --assets can source Tier2 from the same dump) ---
    return writeMapPackage(a, map, outDir, ents, bounds, dumpDir);
}

// ---- convert ------------------------------------------------------------------------------------
// Compatibility alias for fromdump. It accepts only a CoD4 ZoneTool dump folder.
int cmd_convert(const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    std::string inDir = a.pos[0], map = a.pos[1];
    if (!requireMapId(map)) return 2;

    std::string dumpMarker = path_join(path_join(inDir, "maps/mp"), map + ".d3dbsp.ents");
    if (file_exists(dumpMarker)) {
        zt::info("convert: detected ZoneTool dump folder (%s) -> fromdump path", dumpMarker.c_str());
        return cmd_fromdump(a);
    }

    zt::err("convert: input is not a CoD4 ZoneTool dump folder: %s", inDir.c_str());
    return 2;
}

// ---- inspect ------------------------------------------------------------------------------------
int cmd_inspect(const Args& a) {
    if (a.pos.empty()) { usage(); return 2; }
    return inspect_ff(a.pos[0]) ? 0 : 1;
}

int cmd_validatePackage(const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    return validatePackage(a.pos[0], a.pos[1]) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) try {
    Args a;
    if (!parse(argc, argv, a)) { usage(); return 2; }

    if (a.cmd == "read")     return cmd_read(a);
    if (a.cmd == "build") {
        zt::err("build: standalone zone-source input is not supported by this prototype");
        return 2;
    }
    if (a.cmd == "convert")  return cmd_convert(a);
    if (a.cmd == "fromdump") return cmd_fromdump(a);
    if (a.cmd == "inspect")  return cmd_inspect(a);
    if (a.cmd == "validate-package") return cmd_validatePackage(a);

    zt::err("unknown command '%s'", a.cmd.c_str());
    usage();
    return 2;
} catch (const std::exception& error) {
    zt::err("conversion failed: %s", error.what());
    return 1;
}
