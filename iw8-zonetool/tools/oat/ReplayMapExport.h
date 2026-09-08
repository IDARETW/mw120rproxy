// IW8 interchange export for OpenAssetTools (GPL-3.0).
// Uses OAT's own IW4/IW5 types and unpack routines; no retail pointer-layout guesses.
#pragma once
#include "Dumping/AbstractAssetDumper.h"
#include "Game/IW4/IW4.h"
#include "Game/IW4/CommonIW4.h"
#include "Game/IW5/IW5.h"
#include "Game/IW5/CommonIW5.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>

namespace iw8_export {
using Json = nlohmann::json;
inline Json V3(const float* v) {
    return {v[0], v[1], v[2]};
}
inline void Save(AssetDumpingContext& context, const std::string& name, const Json& value) {
    auto f = context.OpenAssetFile(name);
    if (!f)
        throw std::runtime_error("Cannot create IW8 map interchange: " + name);
    *f << value.dump();
    if (!*f)
        throw std::runtime_error("Cannot finish IW8 map interchange: " + name);
}
template <class T> Json Normal(const T& packed) {
    float out[3];
    if constexpr (std::is_same_v<T, IW4::PackedUnitVec>)
        IW4::Common::Vec3UnpackUnitVec(packed, out);
    else
        IW5::Common::Vec3UnpackUnitVec(packed, out);
    return V3(out);
}
template <class T> void Array(const T* pointer, size_t count, size_t maximum, const char* label) {
    if (count > maximum || (count && !pointer))
        throw std::runtime_error(std::string("Invalid ") + label);
}
template <class Asset> class World final : public AbstractAssetDumper<Asset> {
    void DumpAsset(AssetDumpingContext& context,
                   const XAssetInfo<typename Asset::Type>& asset) override {
        const auto& w = *asset.Asset();
        Save(context, std::string(w.name) + ".iw8-world.json", Convert(w));
    }

  public:
    static Json Convert(const typename Asset::Type& w) {
        const auto& d = w.draw;
        Array(w.dpvs.surfaces, w.surfaceCount, 100000, "world surfaces");
        Array(d.vd.vertices, d.vertexCount, 2000000, "world vertices");
        Array(d.indices, d.indexCount, 3000000, "world indices");
        Array(w.models, w.modelCount, 65536, "brush models");
        Array(w.dpvs.smodelDrawInsts, w.dpvs.smodelCount, 100000, "static models");
        constexpr const char* engine =
            std::is_same_v<typename Asset::Type, IW4::GfxWorld> ? "iw4" : "iw5";
        Json j = {{"schema", 1},
                  {"engine", engine},
                  {"name", w.name},
                  {"surfaces", Json::array()},
                  {"models", Json::array()},
                  {"brush_models", Json::array()}};
        for (unsigned i = 0; i < w.surfaceCount; ++i) {
            const auto& s = w.dpvs.surfaces[i];
            if (!s.material || !s.material->info.name ||
                uint64_t(s.tris.firstVertex) + s.tris.vertexCount > d.vertexCount ||
                uint64_t(s.tris.baseIndex) + 3ull * s.tris.triCount > d.indexCount)
                throw std::runtime_error("Invalid world surface references");
            Json out = {{"material", s.material->info.name},
                        {"vertices", Json::array()},
                        {"indices", Json::array()}};
            for (unsigned k = 0; k < s.tris.vertexCount; ++k) {
                const auto& v = d.vd.vertices[s.tris.firstVertex + k];
                out["vertices"].push_back({{"position", V3(v.xyz)},
                                           {"normal", Normal(v.normal)},
                                           {"tangent", Normal(v.tangent)},
                                           {"binormal_sign", v.binormalSign},
                                           {"uv", {v.texCoord[0], v.texCoord[1]}},
                                           {"lightmap_uv", {v.lmapCoord[0], v.lmapCoord[1]}}});
            }
            for (unsigned k = 0; k < 3u * s.tris.triCount; ++k) {
                auto index = d.indices[s.tris.baseIndex + k];
                if (index >= s.tris.vertexCount)
                    throw std::runtime_error("Invalid local world index");
                out["indices"].push_back(index);
            }
            if (s.tris.triCount)
                j["surfaces"].push_back(std::move(out));
            else
                j["surfaces"].push_back({{"material", s.material->info.name},
                                         {"vertices", Json::array()},
                                         {"indices", Json::array()}});
        }
        for (int i = 0; i < w.modelCount; ++i) {
            const auto& m = w.models[i];
            if (uint64_t(m.startSurfIndex) + m.surfaceCount > w.surfaceCount)
                throw std::runtime_error("Invalid brush model surface range");
            j["brush_models"].push_back({{"start", m.startSurfIndex}, {"count", m.surfaceCount}});
        }
        for (unsigned i = 0; i < w.dpvs.smodelCount; ++i) {
            const auto& m = w.dpvs.smodelDrawInsts[i];
            if (!m.model || !m.model->name)
                throw std::runtime_error("Missing static model");
            const auto& p = m.placement;
            j["models"].push_back({{"model", m.model->name},
                                   {"origin", V3(p.origin)},
                                   {"axis", {V3(p.axis[0]), V3(p.axis[1]), V3(p.axis[2])}},
                                   {"scale", p.scale}});
        }
        return j;
    }
};
template <class Asset> class Collision final : public AbstractAssetDumper<Asset> {
    void DumpAsset(AssetDumpingContext& context,
                   const XAssetInfo<typename Asset::Type>& asset) override {
        const auto& c = *asset.Asset();
        Save(context, std::string(c.name) + ".iw8-collision.json", Convert(c));
    }

  public:
    static Json Convert(const typename Asset::Type& c) {
        const auto& info = [&]() -> const auto& {
            if constexpr (std::is_same_v<typename Asset::Type, IW5::clipMap_t>)
                return c.info;
            else
                return c;
        }();
        Array(info.brushes, info.numBrushes, 32768, "collision brushes");
        Array(info.brushBounds, info.numBrushes, 32768, "brush bounds");
        Array(info.brushContents, info.numBrushes, 32768, "brush contents");
        Array(c.verts, c.vertCount, 2000000, "collision vertices");
        Array(c.triIndices, 3ull * c.triCount, 3000000, "collision indices");
        Json j = {{"schema", 1},
                  {"name", c.name},
                  {"brushes", Json::array()},
                  {"vertices", Json::array()},
                  {"triangles", Json::array()}};
        for (unsigned i = 0; i < info.numBrushes; ++i) {
            const auto& b = info.brushes[i];
            const auto& bounds = info.brushBounds[i];
            Array(b.sides, b.numsides, 64, "brush planes");
            Json ps = Json::array();
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned axis = 0; axis < 3; ++axis) {
                    float n[3]{};
                    n[axis] = side ? 1.f : -1.f;
                    const float distance =
                        (side ? bounds.midPoint.v[axis] : -bounds.midPoint.v[axis]) +
                        bounds.halfSize.v[axis];
                    ps.push_back({n[0], n[1], n[2], distance});
                }
            for (unsigned k = 0; k < b.numsides; ++k) {
                if (!b.sides[k].plane)
                    throw std::runtime_error("Missing brush plane");
                const auto& p = *b.sides[k].plane;
                ps.push_back({p.normal[0], p.normal[1], p.normal[2], p.dist});
            }
            j["brushes"].push_back({{"contents", info.brushContents[i]}, {"planes", ps}});
        }
        for (unsigned i = 0; i < c.vertCount; ++i)
            j["vertices"].push_back(V3(c.verts[i].v));
        for (int i = 0; i < c.triCount; ++i) {
            for (int k = 0; k < 3; ++k)
                if (c.triIndices[3 * i + k] >= c.vertCount)
                    throw std::runtime_error("Invalid collision triangle");
            j["triangles"].push_back(
                {c.triIndices[3 * i], c.triIndices[3 * i + 1], c.triIndices[3 * i + 2]});
        }
        return j;
    }
};
} // namespace iw8_export
