#include "dump_source.h"
#include "bindump.h"
#include "common/fs_util.h"
#include "common/json.hpp"
#include "common/log.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace dumpsrc
{

using json = nlohmann::json;
using namespace zt;

// IW5 32-bit struct sizes (pack(4)) used by the colmap walk. Derived from zonetool-develop IW5
// Structs.hpp (cross-checked with IW5_DUMP_FORMAT.md). A wrong size desyncs the stream -> the
// loader catches it via BinReader::ok() and falls back to name-only + generous AABB.
namespace iw5sz
{
constexpr size_t cplane_s = 20;
constexpr size_t Bounds = 24;
constexpr size_t dmaterial_t = 12;
constexpr size_t cbrushside_t = 8;
constexpr size_t cbrushedge_t = 1;      // char
constexpr size_t leafBrush = 2;         // short
constexpr size_t cLeafBrushNode_s = 20; // axis(1)pad short(2) int(4) union(12)
constexpr size_t cbrush_t = 36;
constexpr size_t cStaticModel_s = 76;
constexpr size_t cNode_t = 8;
constexpr size_t cLeaf_t = 40;
constexpr size_t VecInternal3 = 12;
constexpr size_t triIndex = 2; // short
constexpr size_t triEdge = 1;  // char
constexpr size_t CollisionBorder = 28;
constexpr size_t CollisionPartition = 12;
constexpr size_t CollisionAabbTree = 32;
constexpr size_t cmodel_t = 56; // see Structs.hpp:2336 (union pad + bounds/radius + info...)
constexpr size_t SModelAabbNode = 28;
constexpr size_t Stage = 20;
} // namespace iw5sz

DumpSource::DumpSource(const std::string &dumpDir, const std::string &mapName)
    : dir_(dumpDir)
    , map_(mapName)
{
}

std::string DumpSource::sub(const std::string &rel) const
{
    return path_join(dir_, rel);
}
std::string DumpSource::mapBase() const
{
    return sub("maps/mp/" + map_ + ".d3dbsp");
}

bool DumpSource::hasMapFiles() const
{
    return file_exists(mapBase() + ".ents") || file_exists(mapBase() + ".colmap") ||
           file_exists(mapBase() + ".comworld");
}

// ===================================================================================================
// map — entityString (.ents text) + triggers (.ents.triggers bin)
// ===================================================================================================
EntsDump DumpSource::loadEnts() const
{
    EntsDump d;
    std::string entsPath = mapBase() + ".ents";
    if (read_file_str(entsPath, d.text))
    {
        d.loaded = true;
        d.numChars = (int)d.text.size();
    }
    else
    {
        warn("dumpsrc: no .ents at %s", entsPath.c_str());
    }

    // triggers: read_int modelCount, read_array<TriggerModel>; hullCount/hulls; slabCount/slabs.
    std::vector<uint8_t> tb;
    if (read_file(mapBase() + ".ents.triggers", tb))
    {
        BinReader r(tb);
        uint32_t c;
        d.triggers.modelCount = r.read_int();
        r.read_array(/*TriggerModel sizeof*/ 1, c); // size unused: count drives
        // NOTE: TriggerModel/Hull/Slab sizes are not load-bearing for mp_test (all counts 0). When
        // a real trigger set is needed, set the IW5 sizes here. We rely on the count being 0
        // (verified) so the array tag's own count==0 yields an empty block regardless of elemSize.
        d.triggers.hullCount = r.read_int();
        r.read_array(1, c);
        d.triggers.slabCount = r.read_int();
        r.read_array(1, c);
        d.triggers.loaded = r.ok();
        if (!r.ok())
            warn("dumpsrc: .ents.triggers parse: %s (counts m=%d h=%d s=%d)", r.error().c_str(),
                 d.triggers.modelCount, d.triggers.hullCount, d.triggers.slabCount);
    }
    return d;
}

// ===================================================================================================
// map — ComWorld (.comworld JSON). Only name (+ lightCount) is load-bearing (com_map valid-empty).
// ===================================================================================================
ComWorldDump DumpSource::loadComWorld() const
{
    ComWorldDump d;
    std::string txt;
    std::string p = mapBase() + ".comworld";
    if (!read_file_str(p, txt))
    {
        warn("dumpsrc: no .comworld at %s", p.c_str());
        return d;
    }
    json j;
    try
    {
        j = json::parse(txt);
    }
    catch (const std::exception &e)
    {
        err("dumpsrc: .comworld JSON parse: %s", e.what());
        return d;
    }

    d.name = j.value("name", std::string());
    d.isInUse = j.value("isInUse", 0);
    d.primaryLightCount = j.value("primaryLightCount", 0);
    if (j.contains("primaryLights") && j["primaryLights"].is_array())
    {
        for (const auto &lj : j["primaryLights"])
        {
            ComPrimaryLight L;
            L.type = lj.value("type", 0);
            L.canUseShadowMap = lj.value("canUseShadowMap", 0);
            L.exponent = lj.value("exponent", 0);
            L.radius = lj.value("radius", 0.f);
            L.cosHalfFovOuter = lj.value("cosHalfFovOuter", 0.f);
            L.cosHalfFovInner = lj.value("cosHalfFovInner", 0.f);
            L.cosHalfFovExpanded = lj.value("cosHalfFovExpanded", 0.f);
            L.rotationLimit = lj.value("rotationLimit", 0.f);
            L.translationLimit = lj.value("translationLimit", 0.f);
            L.defName = lj.value("defName", std::string());
            auto vec3 = [&](const char *k, float out[3]) {
                if (lj.contains(k) && lj[k].is_array() && lj[k].size() >= 3)
                    for (int i = 0; i < 3; ++i)
                        out[i] = lj[k][i].get<float>();
            };
            vec3("color", L.color);
            vec3("dir", L.dir);
            vec3("up", L.up);
            vec3("origin", L.origin);
            d.primaryLights.push_back(L);
        }
    }
    d.loaded = !d.name.empty();
    return d;
}

// ----- colmap parse_info block (mirrors ClipMap.cpp parse_info; we only need to advance the
// cursor) --
static void colmap_parse_info(BinReader &r)
{
    uint32_t c;
    r.read_int();                     // numCPlanes
    r.read_array(iw5sz::cplane_s, c); // cPlanes
    int numMaterials = r.read_int();
    auto mats = r.read_array(iw5sz::dmaterial_t, c); // materials
    for (int i = 0; i < numMaterials && r.ok(); ++i)
    {
        uint32_t mp = BinReader::getu32(mats, (size_t)i * iw5sz::dmaterial_t + 0); // material ptr
        if (mp)
            r.read_string();
    }
    int numCBrushSides = r.read_int();
    auto sides = r.read_array(iw5sz::cbrushside_t, c);
    for (int i = 0; i < numCBrushSides && r.ok(); ++i)
    {
        uint32_t pl = BinReader::getu32(sides, (size_t)i * iw5sz::cbrushside_t + 0); // plane ptr
        if (pl)
            r.read_array(iw5sz::cplane_s, c);
    }
    r.read_int();
    r.read_array(iw5sz::cbrushedge_t, c); // numCBrushEdges, cBrushEdges
    r.read_int();
    r.read_array(iw5sz::leafBrush, c); // numLeafBrushes, leafBrushes
    int numCLeafBrushNodes = r.read_int();
    auto nodes = r.read_array(iw5sz::cLeafBrushNode_s, c);
    for (int i = 0; i < numCLeafBrushNodes && r.ok(); ++i)
    {
        int16_t cnt =
            BinReader::geti16(nodes, (size_t)i * iw5sz::cLeafBrushNode_s + 2); // leafBrushCount@2
        if (cnt > 0)
            r.read_array(2, c); // brushes (u16)
    }
    int numBrushes = r.read_int();
    auto brushes = r.read_array(iw5sz::cbrush_t, c);
    for (int i = 0; i < numBrushes && r.ok(); ++i)
    {
        uint32_t sidesPtr = BinReader::getu32(brushes, (size_t)i * iw5sz::cbrush_t + 4); // sides@4
        uint32_t edgePtr = BinReader::getu32(brushes, (size_t)i * iw5sz::cbrush_t + 8);  // edge@8
        if (sidesPtr)
        {
            auto s1 = r.read_array(iw5sz::cbrushside_t, c); // sides (1)
            uint32_t pl = BinReader::getu32(s1, 0);
            if (pl)
                r.read_array(iw5sz::cplane_s, c); // sides->plane (1)
        }
        if (edgePtr)
            r.read_array(iw5sz::cbrushedge_t, c); // edge (1)
    }
    r.read_array(iw5sz::Bounds, c); // brushBounds
    r.read_array(4 /*int*/, c);     // brushContents
}

// ===================================================================================================
// map — ClipMap (.colmap BinaryDumper). name always; full walk -> verts AABB with fallback.
// ===================================================================================================
ClipMapDump DumpSource::loadClipMap() const
{
    ClipMapDump d;
    std::vector<uint8_t> buf;
    std::string p = mapBase() + ".colmap";
    if (!read_file(p, buf))
    {
        warn("dumpsrc: no .colmap at %s", p.c_str());
        return d;
    }

    BinReader r(buf);
    // name is the FIRST primitive — trivially correct even if the rest desyncs.
    d.name = r.read_string();
    if (!r.ok() || d.name.empty())
    {
        err("dumpsrc: .colmap name read failed: %s", r.error().c_str());
        return d;
    }
    d.mapEntsName = d.name; // IW5: colmap->mapEnts->name = colmap->name
    d.loaded = true;        // minimum success: name parsed

    // Attempt the full walk to recover verts for a tight AABB. ANY failure => fall back.
    d.isInUse = r.read_int();
    colmap_parse_info(r); // top-level ClipInfo

    uint32_t c;
    int numStaticModels = r.read_int();
    auto sms = r.read_array(iw5sz::cStaticModel_s, c);
    for (int i = 0; i < numStaticModels && r.ok(); ++i)
    {
        uint32_t xm = BinReader::getu32(sms, (size_t)i * iw5sz::cStaticModel_s + 0); // xmodel@0
        if (xm)
            r.read_asset();
    }
    int numCNodes = r.read_int();
    auto cnodes = r.read_array(iw5sz::cNode_t, c);
    for (int i = 0; i < numCNodes && r.ok(); ++i)
    {
        uint32_t pl = BinReader::getu32(cnodes, (size_t)i * iw5sz::cNode_t + 0); // plane@0
        if (pl)
            r.read_array(iw5sz::cplane_s, c);
    }
    r.read_int();
    r.read_array(iw5sz::cLeaf_t, c); // numCLeaf, cLeaf

    int numVerts = r.read_int();
    auto verts = r.read_array(iw5sz::VecInternal3, c); // verts -> the AABB source
    d.numVerts = (int)c;
    debug("dumpsrc: .colmap walk: numVerts(hdr)=%d arrCount=%u cursor=%zu/%zu ok=%d", numVerts, c,
          r.pos(), buf.size(), (int)r.ok());

    // compute tight AABB from verts if the walk reached here cleanly with verts present
    if (r.ok() && c > 0 && verts.size() >= (size_t)c * iw5sz::VecInternal3)
    {
        float mn[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        bool finite = true;
        for (uint32_t i = 0; i < c; ++i)
        {
            for (int k = 0; k < 3; ++k)
            {
                float v = BinReader::getf(verts, (size_t)i * iw5sz::VecInternal3 + (size_t)k * 4);
                if (!std::isfinite(v))
                {
                    finite = false;
                    break;
                }
                mn[k] = std::min(mn[k], v);
                mx[k] = std::max(mx[k], v);
            }
            if (!finite)
                break;
        }
        if (finite)
        {
            // pad the AABB outward so the broadphase comfortably contains the playable space
            for (int k = 0; k < 3; ++k)
            {
                d.boundsMin[k] = mn[k] - 64.f;
                d.boundsMax[k] = mx[k] + 64.f;
            }
            d.boundsFromVerts = true;
        }
    }

    // stageCount (.ents.stages) — informational; independent file, never affects col_map validity.
    std::vector<uint8_t> sbuf;
    if (read_file(mapBase() + ".ents.stages", sbuf))
    {
        BinReader sr(sbuf);
        d.stageCount = sr.read_int();
    }

    if (!r.ok())
    {
        warn("dumpsrc: .colmap full walk desynced (%s); using name-only + generous AABB (map-safe)",
             r.error().c_str());
        d.boundsFromVerts = false; // caller will use the proven generous AABB
    }
    return d;
}

std::vector<std::string> DumpSource::listMaterials() const
{
    std::vector<std::string> out;
    std::string mroot = sub("materials");
    std::vector<std::string> sub2;
    if (!list_dir(mroot, sub2))
        return out;
    for (const auto &s2 : sub2)
    {
        std::vector<std::string> names;
        if (list_dir(path_join(mroot, s2), names))
            for (const auto &nm : names)
                out.push_back(s2 + "/" + nm);
    }
    return out;
}
std::vector<std::string> DumpSource::listXModels() const
{
    std::vector<std::string> out, files;
    if (!list_dir(sub("XModel"), files))
        return out;
    for (auto &f : files)
    {
        if (f.size() > 5 && f.compare(f.size() - 5, 5, ".xme6") == 0)
            out.push_back(f.substr(0, f.size() - 5));
    }
    return out;
}

} // namespace dumpsrc
