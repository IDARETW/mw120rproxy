// Offline fixture uses the actual OAT IW4/IW5 structs and production exporters.
#include "ReplayMapExport.h"
#include <filesystem>
#include <fstream>
#include <iostream>

template <class W,
          class V,
          class S,
          class M,
          class BM,
          class WorldAsset,
          class C,
          class B,
          class Bounds,
          class CollisionAsset>
void fixture(const std::filesystem::path& root, const char* engine) {
    const std::string name = "maps/mp/mp_fixture.d3dbsp";
    W world{};
    V vertices[4]{};
    S surface{};
    M material{};
    BM model{};
    unsigned short indices[6]{0, 1, 2, 0, 2, 3};
    const float positions[4][3]{{-64, -64, 0}, {64, -64, 0}, {64, 64, 0}, {-64, 64, 0}};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j)
            vertices[i].xyz[j] = positions[i][j];
        // IW scale-based signed byte packed unit vectors, using OAT's packer.
        float normal[3]{0, 0, 1}, tangent[3]{1, 0, 0};
        if constexpr (std::is_same_v<W, IW4::GfxWorld>) {
            vertices[i].normal = IW4::Common::Vec3PackUnitVec(normal);
            vertices[i].tangent = IW4::Common::Vec3PackUnitVec(tangent);
        } else {
            vertices[i].normal = IW5::Common::Vec3PackUnitVec(normal);
            vertices[i].tangent = IW5::Common::Vec3PackUnitVec(tangent);
        }
        vertices[i].binormalSign = 1;
        vertices[i].texCoord[0] = i == 1 || i == 2 ? 1.f : 0.f;
        vertices[i].texCoord[1] = i >= 2 ? 1.f : 0.f;
    }
    material.info.name = "fixture_floor";
    surface.material = &material;
    surface.tris.vertexCount = 4;
    surface.tris.triCount = 2;
    world.name = name.c_str();
    world.surfaceCount = 1;
    world.dpvs.surfaces = &surface;
    world.draw.vertexCount = 4;
    world.draw.vd.vertices = vertices;
    world.draw.indexCount = 6;
    world.draw.indices = indices;
    model.surfaceCount = 1;
    world.models = &model;
    world.modelCount = 1;
    auto json = iw8_export::World<WorldAsset>::Convert(world);
    if (json["engine"] != engine || json["surfaces"][0]["indices"].size() != 6)
        throw std::runtime_error("world fixture mismatch");
    // Bounds are a native rectangular slab; six axial planes must survive.
    C collision{};
    B brush{};
    Bounds bounds{};
    int contents = 1;
    bounds.midPoint.v[2] = -4;
    bounds.halfSize.v[0] = 64;
    bounds.halfSize.v[1] = 64;
    bounds.halfSize.v[2] = 4;
    collision.name = name.c_str();
    auto& info = [&]() -> auto& {
        if constexpr (std::is_same_v<C, IW5::clipMap_t>)
            return collision.info;
        else
            return collision;
    }();
    info.numBrushes = 1;
    info.brushes = &brush;
    info.brushBounds = &bounds;
    info.brushContents = &contents;
    auto col = iw8_export::Collision<CollisionAsset>::Convert(collision);
    if (col["brushes"][0]["planes"].size() != 6)
        throw std::runtime_error("collision fixture mismatch");
    auto dir = root / engine / "maps/mp";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "mp_fixture.d3dbsp.iw8-world.json") << json.dump();
    std::ofstream(dir / "mp_fixture.d3dbsp.iw8-collision.json") << col.dump();
    std::ofstream(dir / "mp_fixture.d3dbsp.ents")
        << "{\"classname\" \"worldspawn\"}\n{\"classname\" \"mp_tdm_spawn\" \"origin\" \"0 0 32\"}\n";
    std::filesystem::create_directories(root / engine / "materials");
    std::ofstream(root / engine / "materials/fixture_floor.json") << "{\"textures\":[]}";
    surface.tris.baseIndex = 5;
    try {
        iw8_export::World<WorldAsset>::Convert(world);
        throw std::runtime_error("accepted invalid surface range");
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()) != "Invalid world surface references")
            throw;
    }
    std::cout << engine
              << ": production world/collision export and corrupt-range rejection passed\n";
}
int main(int argc, char** argv) try {
    if (argc != 2)
        return 2;
    fixture<IW4::GfxWorld, IW4::GfxWorldVertex, IW4::GfxSurface, IW4::Material, IW4::GfxBrushModel,
            IW4::AssetGfxWorld, IW4::clipMap_t, IW4::cbrush_t, IW4::Bounds, IW4::AssetClipMapMp>(
        argv[1], "iw4");
    fixture<IW5::GfxWorld, IW5::GfxWorldVertex, IW5::GfxSurface, IW5::Material, IW5::GfxBrushModel,
            IW5::AssetGfxWorld, IW5::clipMap_t, IW5::cbrush_t, IW5::Bounds, IW5::AssetClipMap>(
        argv[1], "iw5");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
