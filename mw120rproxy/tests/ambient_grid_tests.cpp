#include "../ambient_grid.h"
#include "../collision_ray.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
bool Near(float a, float b) {
    return std::abs(a - b) < 1e-5f;
}
std::vector<uint8_t> Bytes(const ambientgrid::Grid& grid) {
    std::vector<uint8_t> bytes(16 + grid.cells.size() * 16 + grid.colors.size() * 12);
    memcpy(bytes.data(), "MWLGRID1", 8);
    const uint32_t cells = uint32_t(grid.cells.size()), colors = uint32_t(grid.colors.size());
    memcpy(bytes.data() + 8, &cells, 4);
    memcpy(bytes.data() + 12, &colors, 4);
    memcpy(bytes.data() + 16, grid.cells.data(), grid.cells.size() * 16);
    memcpy(bytes.data() + 16 + grid.cells.size() * 16, grid.colors.data(), grid.colors.size() * 12);
    return bytes;
}
}
int main() {
    using namespace ambientgrid;
    Grid grid;
    grid.colors = {{.1f, .2f, .3f}, {2, 3, 4}};
    for (int x = -1; x <= 0; ++x)
        for (int y = 0; y <= 1; ++y)
            for (int z = 0; z <= 1; ++z)
                grid.cells.push_back({{x, y, z}, uint16_t(x + 1), 1, 0x55});
    Grid loaded;
    auto bytes = Bytes(grid);
    Check(Parse(bytes, loaded) && loaded.cells.size() == 8, "valid grid parses");
    Check(loaded.cells[0].traceMask == 0x55 && loaded.cells[0].primaryLight == 1,
          "source metadata survives the sidecar");
    Vec result{};
    auto visible = [](const Vec&, const Vec&) {
        return true;
    };
    Check(Sample(loaded, {-16, 16, 32}, visible, result) && Near(result[0], 1.05f) &&
              Near(result[2], 2.15f),
          "negative-coordinate trilinear interpolation");
    Check(Sample(loaded, {-32, 0, 0}, visible, result) && result == grid.colors[0],
          "exact grid point retains authored irradiance");
    collisionfile::Brush wall;
    wall.mins[0] = -9;
    wall.maxs[0] = -7;
    wall.mins[1] = -100;
    wall.maxs[1] = 100;
    wall.mins[2] = -100;
    wall.maxs[2] = 150;
    collisionray::World world;
    Check(world.Build({wall}), "wall visibility fixture builds");
    auto ray = [&](const Vec& a, const Vec& b) {
        collisionray::Hit hit{1};
        return !world.Trace({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, hit, true);
    };
    Check(Sample(loaded, {-16, 16, 32}, ray, result) && Near(result[0], .1f),
          "bright probes across a wall cannot bleed into the room");
    Check(!Sample(loaded, {-8, 16, 32}, ray, result),
          "sampling from inside a solid cannot borrow a visible probe");
    Grid sparse{{{{0, 0, 0}, 0, 0, 0}}, {{.25f, .5f, 1}}};
    Check(Sample(sparse, {40, 0, 0}, visible, result) && Near(result[0], .25f),
          "missing local cells can use a nearby visible probe");
    Check(!Sample(sparse, {0, 0, 160}, visible, result),
          "missing upper floor does not borrow a distant lower-floor probe");
    Check(!Sample(loaded, {NAN, 0, 0}, visible, result), "nonfinite positions rejected");
    for (int kind = 0; kind < 5; ++kind) {
        auto bad = grid;
        if (kind == 0)
            bad.cells[1].coordinate = bad.cells[0].coordinate;
        if (kind == 1)
            bad.cells[0].palette = 2;
        if (kind == 2)
            bad.colors[0][0] = NAN;
        if (kind == 3)
            bad.colors[0][0] = -1;
        if (kind == 4)
            bad.cells[0].coordinate[0] = INT32_MIN;
        Check(!Parse(Bytes(bad), loaded) && loaded.cells.empty(), "invalid data clears output");
    }
    bytes.pop_back();
    Check(!Parse(bytes, loaded), "truncated grid rejected");
    Check(Half(0) == 0 && Half(1) == 0x3C00 && Half(2) == 0x4000 && Half(.5f) == 0x3800 &&
              Half(std::ldexp(1.f, -24)) == 1 && Half(1.9999f) == 0x4000,
          "positive half packing including underflow and mantissa carry");
    const auto probe = Probe({.886226925f, 1.77245385f, .4431134625f});
    uint16_t r, g, b, visibility;
    memcpy(&r, probe.data(), 2);
    memcpy(&g, probe.data() + 18, 2);
    memcpy(&b, probe.data() + 36, 2);
    memcpy(&visibility, probe.data() + 54, 2);
    Check(r == 0x3C00 && g == 0x4000 && b == 0x3800 && visibility == 0x3C00,
          "Replay DC and visibility packing at exact byte offsets");
    Check(ValidProbe(probe), "finite SH probe accepted");
    for (uint16_t invalid : {uint16_t(0x7C00), uint16_t(0xFC00), uint16_t(0x7FFF)}) {
        auto bad = probe;
        memcpy(bad.data() + 26 * 2, &invalid, 2);
        Check(!ValidProbe(bad), "nonfinite SH coefficient rejected before native GPU upload");
    }
    puts(
        "PASS: spatial grid parsing, authored interpolation, real collision occlusion, sparse bounds, malformed data and Replay SH packing");
}
