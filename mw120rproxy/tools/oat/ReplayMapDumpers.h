// Extension for OpenAssetTools v0.33.0 (GPL-3.0). Offline IW3 intermediate export.
// Copy beside ObjWriterIW3.cpp and register the three dumpers there.
#pragma once
#include "Dumping/AbstractAssetDumper.h"
#include "Game/IW3/IW3.h"
#include "Game/IW3/CommonIW3.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include <unordered_map>

namespace replay_export {
using Json = nlohmann::json;
inline Json V3(const float* p) {
    return Json::array({p[0], p[1], p[2]});
}
inline Json Normal(const IW3::PackedUnitVec& p) {
    float n[3];
    IW3::Common::Vec3UnpackUnitVec(p, n);
    return V3(n);
}
inline void Save(AssetDumpingContext& context, const std::string& path, const Json& data) {
    auto file = context.OpenAssetFile(path);
    if (!file)
        throw std::runtime_error("Cannot write Replay intermediate");
    *file << data.dump();
}
inline void LightGrid(AssetDumpingContext& context, const IW3::GfxWorld& world) {
    const auto& grid = world.lightGrid;
    if (!grid.entryCount && !grid.colorCount) {
        Save(context, std::string(world.name) + ".lightgrid.json",
             {{"schema", 1}, {"name", world.name}, {"available", false}});
        return;
    }
    if (grid.rowAxis >= 3 || grid.colAxis >= 3 || grid.rowAxis == grid.colAxis ||
        grid.maxs[grid.rowAxis] < grid.mins[grid.rowAxis] ||
        grid.rawRowDataSize > 64u * 1024 * 1024 || grid.entryCount > 8u * 1024 * 1024 ||
        grid.colorCount > 65536)
        throw std::runtime_error("Invalid IW3 light-grid metadata");
    const size_t rows = grid.maxs[grid.rowAxis] - grid.mins[grid.rowAxis] + 1;
    const std::string stem = std::string(world.name) + ".lightgrid";
    auto binary = [&](const char* suffix, const void* data, size_t bytes) {
        if (bytes && !data)
            throw std::runtime_error("Missing IW3 light-grid array");
        const auto name = stem + suffix;
        auto file = context.OpenAssetFile(name);
        if (!file)
            throw std::runtime_error("Cannot write light-grid array");
        if (bytes)
            file->write(static_cast<const char*>(data), bytes);
        return Json{{"file", name}, {"bytes", bytes}};
    };
    Json out = {{"schema", 1},
                {"name", world.name},
                {"has_light_regions", grid.hasLightRegions},
                {"sun_primary_light_index", grid.sunPrimaryLightIndex},
                {"mins", {grid.mins[0], grid.mins[1], grid.mins[2]}},
                {"maxs", {grid.maxs[0], grid.maxs[1], grid.maxs[2]}},
                {"row_axis", grid.rowAxis},
                {"col_axis", grid.colAxis},
                {"row_count", rows},
                {"entry_count", grid.entryCount},
                {"color_count", grid.colorCount},
                {"row_starts", binary(".rows.bin", grid.rowDataStart, rows * sizeof(uint16_t))},
                {"row_data", binary(".raw.bin", grid.rawRowData, grid.rawRowDataSize)},
                {"entries", binary(".entries.bin", grid.entries,
                                   grid.entryCount * sizeof(IW3::GfxLightGridEntry))},
                {"colors", binary(".colors.bin", grid.colors,
                                  grid.colorCount * sizeof(IW3::GfxLightGridColors))}};
    Save(context, stem + ".json", out);
}
class World final : public AbstractAssetDumper<IW3::AssetGfxWorld> {
    void DumpAsset(AssetDumpingContext& context, const XAssetInfo<IW3::GfxWorld>& asset) override
        try {
        const auto& w = *asset.Asset();
        LightGrid(context, w);
        std::cerr << "Replay world: " << w.name << " surfaces=" << w.surfaceCount
                  << " verts=" << w.vertexCount << " models=" << w.dpvs.smodelCount << std::endl;
        std::cerr << "pointers: surfaces=" << w.dpvs.surfaces << " vertices=" << w.vd.vertices
                  << " indices=" << w.indices << " models=" << w.models << std::endl;
        Json j = {{"schema", 1},
                  {"name", w.name},
                  {"bounds", {V3(w.mins), V3(w.maxs)}},
                  {"sky", w.skyImage ? w.skyImage->name : ""}};
        j["sun"] = {{"color", V3(w.sunColorFromBsp)},
                    {"angles", V3(w.sunParse.angles)},
                    {"intensity", w.sunParse.sunLight},
                    {"ambient", V3(w.sunParse.ambientColor)}};
        j["lightmaps"] = Json::array();
        for (int i = 0; i < w.lightmapCount; ++i) {
            Json pair = Json::array();
            for (const auto* image : {w.lightmaps[i].primary, w.lightmaps[i].secondary}) {
                if (!image || !image->texture.loadDef)
                    throw std::runtime_error("Missing compiled lightmap pixels");
                const auto& pixels = *image->texture.loadDef;
                const auto file = std::string(w.name) + ".lightmap_" + std::to_string(i) + "_" +
                                  std::to_string(pair.size()) + ".bin";
                auto output = context.OpenAssetFile(file);
                if (!output)
                    throw std::runtime_error("Cannot write lightmap");
                output->write(pixels.data, pixels.resourceSize);
                pair.push_back({{"file", file},
                                {"width", image->width},
                                {"height", image->height},
                                {"format", pixels.format},
                                {"bytes", pixels.resourceSize},
                                {"levels", pixels.levelCount}});
            }
            j["lightmaps"].push_back(pair);
        }
        j["surfaces"] = Json::array();
        j["models"] = Json::array();
        j["brush_models"] = Json::array();
        for (int i = 0; i < w.modelCount; ++i) {
            const auto& m = w.models[i];
            j["brush_models"].push_back({{"start", m.startSurfIndex},
                                         {"count", m.surfaceCount},
                                         {"bounds", {V3(m.bounds[0]), V3(m.bounds[1])}}});
        }
        for (int i = 0; i < w.surfaceCount; ++i) {
            const auto& s = w.dpvs.surfaces[i];
            if (i < 3)
                std::cerr << "surface " << i << " mat=" << s.material
                          << " first=" << s.tris.firstVertex << " count=" << s.tris.vertexCount
                          << std::endl;
            if (!s.material)
                throw std::runtime_error("Missing surface material");
            if (i == 0) {
                for (unsigned ti = 0; ti < 34; ++ti) {
                    auto* t = s.material->techniqueSet->techniques[ti];
                    if (!t)
                        continue;
                    for (unsigned pi = 0; pi < t->passCount; ++pi) {
                        auto* ps = t->passArray[pi].pixelShader;
                        if (!ps)
                            continue;
                        auto file = context.OpenAssetFile("replay_shaders/" +
                                                          std::string(ps->name) + ".bin");
                        file->write(reinterpret_cast<const char*>(ps->prog.loadDef.program),
                                    ps->prog.loadDef.programSize * 4);
                    }
                }
            }
            Json out = {{"material", s.material->info.name},
                        {"lightmap", static_cast<unsigned char>(s.lightmapIndex)},
                        {"vertices", Json::array()},
                        {"indices", Json::array()}};
            if (s.tris.firstVertex < 0 ||
                static_cast<unsigned>(s.tris.firstVertex) + s.tris.vertexCount > w.vertexCount ||
                s.tris.baseIndex < 0 || s.tris.baseIndex + 3 * s.tris.triCount > w.indexCount)
                throw std::runtime_error("Invalid IW3 surface range");
            std::unordered_map<unsigned, unsigned> remap;
            for (unsigned k = 0; k < 3u * s.tris.triCount; ++k) {
                const auto index = w.indices[s.tris.baseIndex + k];
                if (index >= s.tris.vertexCount)
                    throw std::runtime_error("Invalid IW3 local surface index: surface=" +
                                             std::to_string(i) + " index=" + std::to_string(index) +
                                             " first=" + std::to_string(s.tris.firstVertex) +
                                             " count=" + std::to_string(s.tris.vertexCount));
                const auto [it, inserted] =
                    remap.emplace(index, static_cast<unsigned>(remap.size()));
                if (inserted) {
                    const auto& v = w.vd.vertices[s.tris.firstVertex + index];
                    out["vertices"].push_back({{"position", V3(v.xyz)},
                                               {"uv", {v.texCoord[0], v.texCoord[1]}},
                                               {"normal", Normal(v.normal)},
                                               {"tangent", Normal(v.tangent)},
                                               {"binormal_sign", v.binormalSign},
                                               {"lightmap_uv", {v.lmapCoord[0], v.lmapCoord[1]}},
                                               {"color",
                                                {static_cast<unsigned char>(v.color.array[0]),
                                                 static_cast<unsigned char>(v.color.array[1]),
                                                 static_cast<unsigned char>(v.color.array[2]),
                                                 static_cast<unsigned char>(v.color.array[3])}}});
                }
                out["indices"].push_back(it->second);
            }
            j["surfaces"].push_back(std::move(out));
            if (i % 512 == 0)
                std::cerr << "exported surface " << i << std::endl;
        }
        for (unsigned i = 0; i < w.dpvs.smodelCount; ++i) {
            const auto& m = w.dpvs.smodelDrawInsts[i];
            const auto& p = m.placement;
            j["models"].push_back(
                {{"model", m.model->name},
                 {"origin", V3(p.origin)},
                 {"axis", {V3(p.axis[0]), V3(p.axis[1]), V3(p.axis[2])}},
                 {"scale", p.scale},
                 {"ground_lighting", w.dpvs.smodelInsts[i].groundLighting.packed}});
        }
        Save(context, std::string(w.name) + ".replay-world.json", j);
    } catch (const std::exception& e) {
        std::cerr << "Replay export failed: " << e.what() << std::endl;
        throw;
    }
};
template <class AssetType> class Collision final : public AbstractAssetDumper<AssetType> {
    void DumpAsset(AssetDumpingContext& context, const XAssetInfo<IW3::clipMap_t>& asset) override {
        const auto& c = *asset.Asset();
        Json j = {{"schema", 1},
                  {"name", c.name},
                  {"brushes", Json::array()},
                  {"vertices", Json::array()},
                  {"triangles", Json::array()},
                  {"static_models", Json::array()},
                  {"submodel_count", c.numSubModels},
                  {"dynamic_model_count", c.dynEntCount[0]},
                  {"dynamic_brush_count", c.dynEntCount[1]},
                  {"submodels", Json::array()},
                  {"leaf_brush_nodes", Json::array()}};
        for (unsigned i = 0; i < c.numSubModels; ++i) {
            const auto& m = c.cmodels[i];
            j["submodels"].push_back(
                {{"mins", V3(m.mins)}, {"maxs", V3(m.maxs)}, {"leaf", m.leaf.leafBrushNode}});
        }
        for (unsigned i = 0; i < c.leafbrushNodesCount; ++i) {
            const auto& n = c.leafbrushNodes[i];
            Json node = {{"count", n.leafBrushCount}, {"brushes", Json::array()}};
            if (n.leafBrushCount > 0)
                for (int k = 0; k < n.leafBrushCount; ++k)
                    node["brushes"].push_back(n.data.leaf.brushes[k]);
            else
                node["children"] = {n.data.children.childOffset[0], n.data.children.childOffset[1]};
            j["leaf_brush_nodes"].push_back(std::move(node));
        }
        for (unsigned i = 0; i < c.numBrushes; ++i) {
            const auto& b = c.brushes[i];
            Json out = {{"mins", V3(b.mins)},
                        {"maxs", V3(b.maxs)},
                        {"contents", b.contents},
                        {"planes", Json::array()},
                        {"ladder_planes", Json::array()}};
            auto ladder = [&](unsigned material, const float* n, float dist) {
                if (material < c.numMaterials && (c.materials[material].surfaceFlags & 8))
                    out["ladder_planes"].push_back({n[0], n[1], n[2], dist});
            };
            for (int side = 0; side < 2; ++side)
                for (int axis = 0; axis < 3; ++axis) {
                    float n[3]{};
                    n[axis] = side ? 1.f : -1.f;
                    ladder(b.axialMaterialNum[side][axis], n, side ? b.maxs[axis] : -b.mins[axis]);
                }
            for (unsigned k = 0; k < b.numsides; ++k) {
                const auto& p = *b.sides[k].plane;
                out["planes"].push_back({p.normal[0], p.normal[1], p.normal[2], p.dist});
                ladder(b.sides[k].materialNum, p.normal, p.dist);
            }
            j["brushes"].push_back(std::move(out));
        }
        for (unsigned i = 0; i < c.vertCount; ++i)
            j["vertices"].push_back(V3(c.verts[i].v));
        for (int i = 0; i < c.triCount; ++i)
            j["triangles"].push_back(
                {c.triIndices[i * 3], c.triIndices[i * 3 + 1], c.triIndices[i * 3 + 2]});
        std::vector<uint32_t> triangleContents(c.triCount);
        std::vector<bool> mappedTriangles(c.triCount);
        for (int i = 0; i < c.aabbTreeCount; ++i) {
            const auto& tree = c.aabbTrees[i];
            if (tree.childCount)
                continue;
            if (tree.materialIndex >= c.numMaterials || tree.u.partitionIndex < 0 ||
                tree.u.partitionIndex >= c.partitionCount)
                throw std::runtime_error("Invalid collision tree material or partition");
            const auto& partition = c.partitions[tree.u.partitionIndex];
            if (partition.firstTri < 0 || partition.firstTri > c.triCount ||
                partition.triCount > c.triCount - partition.firstTri)
                throw std::runtime_error("Invalid collision partition triangle span");
            const auto contents = uint32_t(c.materials[tree.materialIndex].contentFlags);
            for (int t = partition.firstTri; t < partition.firstTri + partition.triCount; ++t) {
                triangleContents[t] |= contents;
                mappedTriangles[t] = true;
            }
        }
        j["triangle_contents_schema"] = "material-partitions-v1";
        j["triangle_contents"] = Json::array();
        for (int i = 0; i < c.triCount; ++i)
            j["triangle_contents"].push_back(mappedTriangles[i] ? Json(triangleContents[i])
                                                                : Json(nullptr));
        for (unsigned i = 0; i < c.numStaticModels; ++i) {
            const auto& m = c.staticModelList[i];
            j["static_models"].push_back({{"model", m.xmodel->name}, {"origin", V3(m.origin)}});
        }
        Save(context, std::string(c.name) + ".replay-collision.json", j);
    }
};
} // namespace replay_export
