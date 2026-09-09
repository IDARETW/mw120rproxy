#include "../src/iw8/replay_bounds.h"
#include <cstdio>
#include <cstdlib>

void Check(bool valid, const char* name) {
    if (!valid) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}

int main() {
    using namespace replaybounds;
    Accumulator bounds;
    bounds.Add({-3520, -9216, -512});
    bounds.Add({7104, 4288, 1024});
    auto result = bounds.Finish();
    Check(result.midpoint == std::array<float, 3>{1792, -2464, 256} &&
              result.halfSize == std::array<float, 3>{5312, 6752, 768},
          "stock Frontend world model extents");
    Check(result.Radius() == 8625.3505859375f, "stock Frontend world model radius");
    float wire[6];
    result.Write(wire, 1);
    Check(wire[0] == 1792 && wire[3] == 5313 && wire[4] == 6753 && wire[5] == 769,
          "stock world bounds add one unit of padding");
    Accumulator scene, draw;
    for (const auto p : {std::array<float, 3>{-12, 8, 5}, std::array<float, 3>{48, 64, 5}}) {
        scene.Add(p);
        draw.Add(p);
    }
    draw.Add({-32768, -32768, -32768});
    draw.Add({32768, 32768, 32768});
    Check(scene.Finish().halfSize[0] == 30 && draw.Finish().halfSize[0] == 32768,
          "synthetic sky does not enlarge scene bounds while remaining in draw bounds");
    Check(scene.Finish().halfSize[2] == 0, "planar geometry bounds remain valid");
    Accumulator rounding;
    const float lo = 99999.96875f, hi = 99999.9921875f;
    rounding.Add({lo, 0, 0});
    rounding.Add({hi, 0, 0});
    const auto r = rounding.Finish();
    Check(double(r.midpoint[0]) - r.halfSize[0] <= lo &&
              double(r.midpoint[0]) + r.halfSize[0] >= hi,
          "rounded bounds enclose large-coordinate vertices");
    bool rejected = false;
    try {
        Accumulator{}.Finish();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Check(rejected, "missing non-sky geometry rejected");
    for (float value : {std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN(), 100001.f}) {
        rejected = false;
        try {
            bounds.Add({value, 0, 0});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        Check(rejected, "invalid source coordinates rejected");
    }
    puts(
        "PASS: stock bounds/radius, sky isolation, planar geometry, conservative rounding and invalid input");
}
