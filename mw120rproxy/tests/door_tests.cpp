#include "custom_doors.h"
#include "door_file.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace customphysics {
bool OwnsEmptyWorld() {
    return true;
}
}
namespace {
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void Native(int world,
            void* result,
            const float*,
            const float*,
            const float*,
            const int*,
            int count,
            int children,
            int mask,
            int locational,
            const unsigned char*,
            int phase) {
    Check(world == 0 && count == 1 && children == 1 && mask == 0x2806191 && !locational && !phase,
          "door LOS uses original world trace ABI");
    const float fraction = 1;
    std::memcpy(result, &fraction, 4);
}
std::vector<unsigned char> Fixture() {
    std::vector<unsigned char> data;
    auto append = [&](const void* p, size_t n) {
        auto b = static_cast<const unsigned char*>(p);
        data.insert(data.end(), b, b + n);
    };
    append("MWRDOR01", 8);
    unsigned header[]{1, 49};
    append(header, sizeof(header));
    char name[64] = "door_test";
    append(name, 64);
    unsigned info[]{0, 49};
    append(info, sizeof(info));
    float values[]{0, -30, 0, 90, 0, 0, 0, -2, -30, 0, 2, 30, 80, .9f};
    append(values, sizeof(values));
    unsigned hulls = 1, planes = 6;
    append(&hulls, 4);
    append(&planes, 4);
    float hull[]{1, 0, 0, 2, -1, 0, 0, 2, 0, 1, 0, 30, 0, -1, 0, 30, 0, 0, 1, 80, 0, 0, -1, 0};
    append(hull, sizeof(hull));
    for (unsigned i = 0; i < 49; ++i) {
        unsigned count = 1;
        append(&count, 4);
        append(&i, 4);
    }
    return data;
}
float Trace(const float* a, const float* b, bool melee = false, float nativeLimit = 1) {
    unsigned char result[0x48]{};
    const float bounds[6]{};
    std::memcpy(result, &nativeLimit, 4);
    customdoors::Trace(result, a, b, bounds, melee ? 0x2806191 : 1, melee);
    float out;
    std::memcpy(&out, result, 4);
    return out;
}
}
int main(int argc, char** argv) {
    if (argc == 2) {
        std::vector<doorfile::Door> doors;
        unsigned surfaces = 0;
        Check(doorfile::Load(argv[1], doors, surfaces),
              "converted map door sidecar passes native parser");
        for (const auto& d : doors)
            std::printf("door=%s hulls=%zu poses=%zu\n", d.name, d.hulls.size(), d.surfaces.size());
        return 0;
    }
    const auto path = std::filesystem::temp_directory_path() /
                      ("mw120r-door-tests-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(path);
    auto bytes = Fixture();
    auto write = [&] {
        std::ofstream f(path / "doors.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    write();
    std::vector<doorfile::Door> doors;
    unsigned surfaces = 0;
    Check(doorfile::Load(path / "doors.bin", doors, surfaces) && surfaces == 49 &&
              doors.size() == 1,
          "valid door fixture");
    const float a[]{-50, 0, 50}, b[]{50, 0, 50}, bounds[6]{};
    float fraction;
    doorfile::Vec normal;
    Check(doorfile::Hit(doors[0], 24, a, b, bounds, 1, fraction, normal) && fraction > .47f &&
              fraction < .49f,
          "closed door blocks shots at exact plane");
    Check(!doorfile::Hit(doors[0], 0, a, b, bounds, 1, fraction, normal), "open doorway is clear");
    const float c[]{30, -60, 50}, d[]{30, 0, 50};
    Check(doorfile::Hit(doors[0], 0, c, d, bounds, 1, fraction, normal),
          "open leaf remains solid in rotated position");
    const float outside[]{-50, 100, 50}, outsideEnd[]{50, 100, 50};
    Check(!doorfile::Hit(doors[0], 24, outside, outsideEnd, bounds, 1, fraction, normal),
          "finite door does not block beside doorway");
    customdoors::Initialize(0, &Native);
    customdoors::Load(path);
    Check(Trace(a, b) < 1 && Trace(a, b, false, .2f) == .2f,
          "world occluders keep the closest hit");
    std::array<unsigned char, 0x400> pm{}, ps{};
    float pml[]{1, 0, 0};
    uintptr_t pointer = reinterpret_cast<uintptr_t>(ps.data());
    std::memcpy(pm.data() + 8, &pointer, 8);
    int type = 0;
    std::memcpy(ps.data() + 0xC, &type, 4);
    float origin[]{-40, 0, 0};
    std::memcpy(ps.data() + 0x30, origin, 12);
    auto move = [&](int time, uint64_t buttons) {
        std::memcpy(pm.data() + 0x10, &buttons, 8);
        std::memcpy(pm.data() + 0x1C, &time, 4);
        customdoors::Movement(pm.data(), pml);
    };
    move(1000, 0);
    Check(!customdoors::Hint().empty(), "in-range door produces interaction hint");
    move(1010, 8);
    for (int t = 1060; t <= 2060; t += 50)
        move(t, 8);
    Check(Trace(a, b) == 1, "keyboard Activate opens; holding Activate does not toggle again");
    std::array<unsigned char, 0x4200> world{};
    unsigned mask[2]{~0u, ~0u};
    uintptr_t vis = reinterpret_cast<uintptr_t>(mask);
    std::memcpy(world.data() + 0xC8, &surfaces, 4);
    std::memcpy(world.data() + 0x40B8, &vis, 8);
    customdoors::Visibility(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(mask[0] == 0x80000000u && (mask[1] & 0xFFFF8000u) == 0,
          "only the active render pose remains visible");
    pml[0] = .8f;
    pml[1] = -.6f;
    move(2110, 0);
    move(2160, 32);
    // A player enters the doorway while it is closing. The door must reopen.
    origin[0] = 0;
    std::memcpy(ps.data() + 0x30, origin, 12);
    for (int t = 2210; t <= 4260; t += 50)
        move(t, 0);
    Check(Trace(a, b) == 1, "closing on a player reverses instead of trapping them");
    origin[0] = -40;
    std::memcpy(ps.data() + 0x30, origin, 12);
    move(4310, 0);
    move(4360, 32);
    for (int t = 4410; t <= 5460; t += 50)
        move(t, 0);
    Check(Trace(a, b) < 1, "Use closes the door after the doorway clears");
    Check(Trace(a, b, true) < 1, "melee registers a physical hit before bashing");
    for (int t = 5510; t <= 5910; t += 50)
        move(t, 0);
    Check(Trace(a, b) == 1, "melee bash opens faster than regular use");
    customdoors::Load(path);
    Check(Trace(a, b) < 1, "map reload resets doors to closed");
    pml[0] = 1;
    pml[1] = 0;
    move(6000, 16);
    for (int t = 6050; t <= 7050; t += 50)
        move(t, 16);
    Check(Trace(a, b) < 1, "Reload alone never operates doors");
    move(7100, 0);
    move(7150, 32);
    for (int t = 7200; t <= 8200; t += 50)
        move(t, 32);
    Check(Trace(a, b) == 1, "controller Use/Reload still opens doors");
    customdoors::Load(path);
    origin[0] = -20;
    std::memcpy(ps.data() + 0x30, origin, 12);
    const float velocity[]{240, 0, 0};
    std::memcpy(ps.data() + 0x3C, velocity, 12);
    move(9000, 0);
    for (int t = 9050; t <= 9400; t += 50)
        move(t, 0);
    Check(Trace(a, b) == 1, "sprint-speed contact bashes without pressing Use");
    customdoors::Clear();
    Check(Trace(a, b) == 1, "map unload removes supplemental door collision");
    bytes.back() = 255;
    write();
    Check(!doorfile::Load(path / "doors.bin", doors, surfaces),
          "reject out-of-range surface index");
    bytes = Fixture();
    bytes.resize(48);
    write();
    Check(!doorfile::Load(path / "doors.bin", doors, surfaces), "reject truncated door data");
    std::filesystem::remove(path / "doors.bin");
    std::filesystem::remove(path);
    std::puts(
        "PASS: door use/close/bash, held-input suppression, player obstruction, rotated collision, visibility, lifecycle and malformed sidecars");
}
