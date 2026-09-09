#include "../collision_ray.h"
#include "../compound_collision.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
int main(int argc, char** argv) {
    using namespace collisionray;
    collisionfile::Brush box;
    box.mins[0] = box.mins[1] = box.mins[2] = -1;
    box.maxs[0] = box.maxs[1] = box.maxs[2] = 1;
    World world;
    Check(world.Build({box}), "box build");
    Hit hit;
    Check(world.Trace({-3, 0, 0}, {3, 0, 0}, hit) && std::abs(hit.fraction - 1. / 3) < 1e-6 &&
              hit.normal[0] == -1,
          "front wall hit");
    hit = {};
    Check(world.Trace({0, 0, 3}, {0, 0, -3}, hit) && hit.normal[2] == 1, "floor hit");
    hit = {};
    Check(world.Trace({3, 0, 0}, {-3, 0, 0}, hit) && hit.normal[0] == 1,
          "reverse penetration ray hit");
    hit = {.1};
    Check(!world.Trace({-3, 0, 0}, {3, 0, 0}, hit), "nearer native hit retained");
    hit = {};
    Check(!world.Trace({-3, 2, 0}, {3, 2, 0}, hit), "open space stays open");
    hit = {};
    Check(world.Trace({0, 0, 0}, {3, 0, 0}, hit) && hit.startSolid && !hit.allSolid &&
              hit.fraction == 0,
          "bullet starting inside solid");
    hit = {};
    Check(!world.Trace({0, 0, 0}, {3, 0, 0}, hit, false), "legacy inside handling retained");
    hit = {};
    Check(world.Trace({0, 0, 0}, {.5, 0, 0}, hit) && hit.allSolid, "fully embedded bullet");
    // A thin triangle prism, representative of compiled collision triangles.
    box.vertices = {{{0, 0, -.125f}}, {{10, 0, -.125f}}, {{0, 10, -.125f}},
                    {{0, 0, .125f}},  {{10, 0, .125f}},  {{0, 10, .125f}}};
    for (unsigned k = 0; k < 3; ++k) {
        box.mins[k] = 100;
        box.maxs[k] = -100;
        for (const auto& p : box.vertices) {
            box.mins[k] = std::min(box.mins[k], p[k]);
            box.maxs[k] = std::max(box.maxs[k], p[k]);
        }
    }
    Check(world.Build({box}), "thin triangular hull build");
    hit = {};
    Check(!world.Trace({8, 8, -10}, {8, 8, 10}, hit), "AABB empty corner is not a false wall");
    hit = {};
    Check(world.Trace({2, 2, -10000}, {2, 2, 10000}, hit) && hit.normal[2] == -1,
          "long bullet cannot tunnel through quarter-unit geometry");
    Check(!world.Build({}) && !world.Trace({2, 2, -10}, {2, 2, 10}, hit), "clear stale tree");
    auto clip = box;
    clip.contents = collisionfile::PlayerClip;
    Check(world.Build({clip}), "player clip hull build");
    hit = {};
    Check(!world.Trace({2, 2, -10}, {2, 2, 10}, hit),
          "player clip does not obstruct solid-only bullet or lighting rays");
    Check(world.Trace({2, 2, -10}, {2, 2, 10}, hit, true, collisionfile::PlayerClip) &&
              hit.contents == collisionfile::PlayerClip,
          "player trace retains the clip contact contents");
    clip.contents = collisionfile::ShotClip;
    Check(world.Build({clip}), "shot clip hull build");
    hit = {};
    Check(!world.Trace({2, 2, -10}, {2, 2, 10}, hit, true,
                       collisionfile::Solid | collisionfile::PlayerClip),
          "shot clip does not become a movement wall");
    Check(world.Trace({2, 2, -10}, {2, 2, 10}, hit, true, 0x2806931) &&
              hit.contents == collisionfile::ShotClip,
          "native bullet contents mask includes shot-only geometry");
    std::vector<collisionfile::Brush> mixed(513, box);
    for (size_t i = 0; i < mixed.size(); ++i)
        mixed[i].contents = i % 3 == 0   ? collisionfile::Solid
                            : i % 3 == 1 ? collisionfile::PlayerClip
                                         : collisionfile::ShotClip;
    const auto batches = compoundcollision::Batches(mixed);
    Check(batches.size() == 3, "compound batches group interleaved collision classes");
    std::vector<bool> present(mixed.size());
    for (const auto& batch : batches) {
        Check(batch.brushes.size() <= compoundcollision::BatchSize, "bounded compound batch");
        for (size_t i : batch.brushes) {
            Check(!present[i] && mixed[i].contents == batch.contents,
                  "compound children are unique and share body contents");
            present[i] = true;
        }
    }
    Check(std::all_of(present.begin(), present.end(),
                      [](bool v) {
                          return v;
                      }),
          "typed compound batching preserves every source hull");
    std::vector<uint8_t> typed(20 + box.vertices.size() * 12);
    memcpy(typed.data(), "MWCOLL03", 8);
    uint32_t one = 1, vertices = uint32_t(box.vertices.size()),
             contents = collisionfile::PlayerClip;
    memcpy(typed.data() + 8, &one, 4);
    memcpy(typed.data() + 12, &vertices, 4);
    memcpy(typed.data() + 16, &contents, 4);
    memcpy(typed.data() + 20, box.vertices.data(), box.vertices.size() * 12);
    std::vector<collisionfile::Brush> parsed;
    Check(collisionfile::Parse(typed, parsed) && parsed[0].contents == contents,
          "version 3 round-trips typed convex geometry");
    auto legacy = typed;
    legacy.erase(legacy.begin() + 16, legacy.begin() + 20);
    legacy[7] = '2';
    Check(collisionfile::Parse(legacy, parsed) && parsed[0].contents == collisionfile::Solid,
          "version 2 keeps legacy solid semantics");
    typed[18] = 0x80;
    Check(!collisionfile::Parse(typed, parsed) && parsed.empty(),
          "unsupported contents fail closed without stale parsed hulls");
    puts(
        "PASS: walls, floors, reverse rays, nearer hits, open space, inside handling, thin convex geometry and reload");
    if (argc < 2)
        return 0;
    std::vector<collisionfile::Brush> brushes;
    Check(collisionfile::Load(argv[1], brushes), "installed collision package parses");
    for (unsigned i = 0; i < brushes.size(); ++i) {
        Hull hull;
        if (!BuildHull(brushes[i], hull)) {
            fprintf(stderr, "invalid hull %u vertices=%zu\n", i, brushes[i].vertices.size());
            for (const auto& p : brushes[i].vertices)
                fprintf(stderr, "%.9g %.9g %.9g\n", p[0], p[1], p[2]);
            Check(false, "installed convex reconstruction");
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    Check(world.Build(brushes), "every installed collision hull builds");
    const auto built = std::chrono::steady_clock::now();
    unsigned count = 0;
    if (argc >= 3) {
        std::ifstream f(argv[2], std::ios::binary);
        Check(bool(f), "independent ray reference opens");
        float row[7];
        while (f.read(reinterpret_cast<char*>(row), sizeof(row))) {
            hit = {};
            world.Trace({row[0], row[1], row[2]}, {row[3], row[4], row[5]}, hit);
            if (std::abs(hit.fraction - row[6]) > 2e-5) {
                fprintf(stderr, "ray=%u expected=%.9f actual=%.9f\n", count, row[6], hit.fraction);
                Check(false, "independent reference fraction");
            }
            ++count;
        }
        Check(f.eof() && count > 0, "reference ray count");
    }
    printf("PASS: %zu installed hulls; build %.1f ms; %u independent ray references\n",
           brushes.size(), std::chrono::duration<double, std::milli>(built - begin).count(), count);
}
