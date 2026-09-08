#include "custom_surfaces.h"
void CustomSurfaceTests() {
    const auto dir = packageRoot / id;
    const auto file = dir / "footsteps.bin";
    std::array<customsurfaces::Triangle, 2> triangles{
        {{{{0, 0, 0}, {0, 64, 0}, {64, 0, 0}}, 21},
         {{{0, 0, 128}, {0, 64, 128}, {64, 0, 128}}, 13}}};
    {
        std::ofstream f(file, std::ios::binary);
        f.write("MWRSTEP1", 8);
        unsigned count = 2;
        f.write((char*)&count, 4);
        f.write((char*)triangles.data(), sizeof(triangles));
    }
    customsurfaces::Load(dir);
    auto d = customsurfaces::data.load();
    Check(d && d->triangles.size() == 2, "footstep sidecar loads authored surface data");
    float point[]{16, 16, 1};
    Check(customsurfaces::TypeAt(*d, point) == 21, "wood ground lookup selects lower floor");
    point[2] = 129;
    Check(customsurfaces::TypeAt(*d, point) == 13, "metal upstairs lookup rejects floor below");
    point[2] = 60;
    Check(customsurfaces::TypeAt(*d, point) == 5, "unsupported height uses concrete fallback");
    point[0] = -100;
    Check(customsurfaces::TypeAt(*d, point) == 5, "unmapped ground uses concrete fallback");
    {
        std::ofstream f(file, std::ios::binary | std::ios::trunc);
        f.write("MWRSTEP1", 8);
    }
    customsurfaces::Load(dir);
    Check(!customsurfaces::data.load(), "truncated footstep data rejected without stale map data");
    fs::remove(file);
    customsurfaces::Load(dir);
    Check(customsurfaces::data.load()->triangles.empty(),
          "older map packages retain concrete fallback");
    customsurfaces::Clear();
    Check(!customsurfaces::data.load(), "map unload clears footstep geometry");
    puts(
        "PASS: authored wood/metal floor lookup, vertical separation, concrete fallback, malformed sidecar and map unload");
}
