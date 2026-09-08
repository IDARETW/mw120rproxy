float glassStockFraction = 1;
unsigned glassFxCalls = 0, glassSoundCalls = 0;
const char* glassEffectName = "vfx/core/impacts/small_glass";
struct GlassAlias {
    const char* name = "glass_pane_breakout";
    unsigned id = 1234;
} glassAlias;
void* GlassFindEffect(int type, const char* name, int allowDefault) {
    Check(type == 44 && !strcmp(name, glassEffectName) && !allowDefault,
          "look up exact installed glass particle asset");
    return &glassEffectName;
}
void* GlassFindSound(const char* name) {
    Check(!strcmp(name, glassAlias.name), "look up installed glass breakout alias");
    return &glassAlias;
}
unsigned GlassEffect(int client, void* ref, int time, const float* origin, const float* axis) {
    Check(client == 0 && time == 1000 && *static_cast<void**>(ref) == &glassEffectName &&
              std::abs(origin[0] - 2) < .001f && axis[0] == 1,
          "native shatter effect receives current client time and surface-offset impact");
    ++glassFxCalls;
    return 0x1001;
}
void GlassBasis(const float*, float* right, float* up) {
    right[0] = 0;
    right[1] = 1;
    right[2] = 0;
    up[0] = up[1] = 0;
    up[2] = 1;
}
void GlassSound(unsigned id, int client, int ent, const float* origin) {
    Check(id == 1234 && client == 0 && ent == 2046 && std::abs(origin[0]) < .001f,
          "native sound queue receives spatial pane impact");
    ++glassSoundCalls;
}
void GlassStockPhysics(int world,
                       void* result,
                       const float*,
                       const float*,
                       const float*,
                       const int* skip,
                       int count,
                       int children,
                       int mask,
                       int locational,
                       const unsigned char* priority,
                       int phase) {
    Check(world == 1 && !skip && count == 0 && children == 0 && mask == 0x2806931 &&
              locational == 1 && !priority && phase == 0,
          "physics bullet trace retains all twelve arguments");
    memset(result, 0, 0x48);
    memcpy(result, &glassStockFraction, 4);
}
void GlassStockSlide(void*,
                     void*,
                     void* result,
                     const float*,
                     const float*,
                     const float*,
                     int pass,
                     unsigned*,
                     unsigned count,
                     int mask,
                     bool cheap) {
    Check(pass == 7 && count == 0 && mask == 1 && cheap, "slide trace retains eleven arguments");
    memset(result, 0, 0x48);
    memcpy(result, &glassStockFraction, 4);
}
bool GlassStockBullet(void*, const void*, bool alt, void*, void* result, int previous, bool self) {
    Check(alt && previous == 19 && self, "bullet trace retains seven arguments");
    memset(result, 0, 0x48);
    memcpy(result, &glassStockFraction, 4);
    return true;
}
void GlassStockLegacy(
    void*, void*, void* result, const float*, const float*, const float*, int pass, int mask) {
    Check(pass == 7 && mask == 16, "mantle glass query retains eight arguments");
    memset(result, 0, 0x48);
    memcpy(result, &glassStockFraction, 4);
}
void GlassTests() {
    auto dir = packageRoot / id;
    {
        std::ofstream f(dir / "glass.bin", std::ios::binary);
        f.write("MWRGLS01", 8);
        unsigned head[]{2, 4};
        f.write(reinterpret_cast<char*>(head), 8);
        for (unsigned i = 0; i < 2; ++i) {
            unsigned counts[]{4, 1};
            f.write(reinterpret_cast<char*>(counts), 8);
            float x = float(i * 80);
            float data[]{1, 0, 0, x, -20, 0, x, 20, 0, x, 20, 80, x, -20, 80};
            f.write(reinterpret_cast<char*>(data), sizeof(data));
            f.write(reinterpret_cast<char*>(&i), 4);
        }
    }
    customglass::Load(dir);
    for (uintptr_t rva : {0xEEF1288, 0xF26F940})
        Commit(rva);
    std::vector<unsigned char> cg(0x6600);
    int connected = 9, time = 1000;
    auto* cgPtr = cg.data();
    memcpy(image + 0xEEF1288, &connected, 4);
    memcpy(image + 0xF26F940, &cgPtr, 8);
    memcpy(cg.data() + 0x65A4, &time, 4);
    for (const auto* b :
         {&replay::PlayOrientedEffect, &replay::NormalBasis, &replay::SoundAliasByName,
          &replay::SoundAtPosition, &replay::FindAsset}) {
        Commit(b->rva);
        memcpy(image + b->rva, b->bytes, b->size);
    }
    for (const auto* b : {&replay::PhysicsBulletTrace, &replay::PhysicsLegacyTrace}) {
        Commit(b->rva);
        PrologueFixture(*b, {0x48, 0x83, 0xC4, 0x78}, reinterpret_cast<void*>(&GlassStockPhysics));
    }
    for (auto rva : {replay::BulletTrace.rva, replay::LegacySlideTrace.rva, replay::LegacyTrace.rva,
                     uintptr_t(0xEE55C98), uintptr_t(0x2437AFA)})
        Commit(rva);
    PrologueFixture(replay::BulletTrace,
                    {0x48, 0x83, 0xC4, 0x60, 0x41, 0x5E, 0x5F, 0x5E, 0x5D, 0x5B},
                    reinterpret_cast<void*>(&GlassStockBullet));
    PrologueFixture(replay::LegacySlideTrace, {0x48, 0x81, 0xC4, 0x90, 0, 0, 0, 0x5F},
                    reinterpret_cast<void*>(&GlassStockSlide));
    PrologueFixture(replay::LegacyTrace, {0x48, 0x83, 0xC4, 0x60, 0x5B},
                    reinterpret_cast<void*>(&GlassStockLegacy));
    Check(customglass::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "glass hooks match pinned Replay prologues");
    Stub(replay::FindAsset, &GlassFindEffect);
    Stub(replay::PlayOrientedEffect, &GlassEffect);
    Stub(replay::NormalBasis, &GlassBasis);
    Stub(replay::SoundAliasByName, &GlassFindSound);
    Stub(replay::SoundAtPosition, &GlassSound);
    auto slide = reinterpret_cast<void (*)(void*, void*, void*, const float*, const float*,
                                           const float*, int, unsigned*, unsigned, int, bool)>(
        image + replay::LegacySlideTrace.rva);
    auto bullet = reinterpret_cast<bool (*)(void*, const void*, bool, void*, void*, int, bool)>(
        image + replay::BulletTrace.rva);
    std::array<unsigned char, 0x48> trace{};
    float start[]{40, 0, 10}, end[]{-40, 0, 10}, bounds[]{0, 0, 30, 15, 15, 30};
    slide(nullptr, nullptr, trace.data(), start, end, bounds, 7, nullptr, 0, 1, true);
    Check(*reinterpret_cast<float*>(trace.data()) > .30f &&
              *reinterpret_cast<float*>(trace.data()) < .32f,
          "intact glass blocks player capsule before plane");
    std::array<unsigned char, 0x90> bp{};
    float rayA[]{40, 0, 40}, rayB[]{-40, 0, 40};
    memcpy(bp.data() + 0x68, rayA, 12);
    memcpy(bp.data() + 0x74, rayB, 12);
    glassStockFraction = .1f;
    bullet(bp.data(), nullptr, true, nullptr, trace.data(), 19, true);
    glassStockFraction = 1;
    slide(nullptr, nullptr, trace.data(), start, end, bounds, 7, nullptr, 0, 1, true);
    Check(*reinterpret_cast<float*>(trace.data()) < 1,
          "wall occlusion prevents breaking glass behind the hit");
    Check(bullet(bp.data(), nullptr, true, nullptr, trace.data(), 19, true),
          "native bullet result retained");
    customglass::PumpEffects();
    customglass::PumpEffects();
    Check(glassFxCalls == 1 && glassSoundCalls == 1,
          "shot produces one native shatter effect and positional sound, never duplicates");
    slide(nullptr, nullptr, trace.data(), start, end, bounds, 7, nullptr, 0, 1, true);
    Check(*reinterpret_cast<float*>(trace.data()) == 1, "broken pane releases collision");
    std::vector<unsigned char> world(0x4300);
    unsigned n = 4, visibility = 0xF0000000;
    auto* vis = &visibility;
    memcpy(world.data() + 0xC8, &n, 4);
    memcpy(world.data() + 0x40B8, &vis, 8);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(visibility == 0x70000000, "only broken pane surfaces become invisible");
    customglass::Load(dir);
    visibility = 0xF0000000;
    customglass::HideBroken(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(visibility == 0xF0000000, "match reload resets every pane");
    // Reproduce the native Mantle_Move call site, forwarding all stack arguments.
    std::vector<unsigned char> prefix{0x48, 0x83, 0xEC, 0x48};
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned char a[]{0x48, 0x8B, 0x84, 0x24, static_cast<unsigned char>(0x70 + 8 * i),
                                0,    0,    0};
        prefix.insert(prefix.end(), a, a + 8);
        const unsigned char b[]{0x48, 0x89, 0x44, 0x24, static_cast<unsigned char>(0x20 + 8 * i)};
        prefix.insert(prefix.end(), b, b + 5);
    }
    const uintptr_t call = 0x11076A9, entry = call - prefix.size();
    Commit(entry);
    memcpy(image + entry, prefix.data(), prefix.size());
    image[call] = 0xE8;
    const int displacement = int(replay::LegacyTrace.rva - call - 5);
    memcpy(image + call + 1, &displacement, 4);
    const unsigned char after[]{0x48, 0x83, 0xC4, 0x48, 0xC3};
    memcpy(image + call + 5, after, 5);
    FlushInstructionCache(GetCurrentProcess(), image, 0x1324B000);
    using Legacy =
        void (*)(void*, void*, void*, const float*, const float*, const float*, int, int);
    auto direct = reinterpret_cast<Legacy>(image + replay::LegacyTrace.rva),
         mantle = reinterpret_cast<Legacy>(image + entry);
    float overlap[]{0, 0, 10};
    direct(nullptr, nullptr, trace.data(), overlap, overlap, bounds, 7, 16);
    visibility = 0xF0000000;
    customglass::HideBroken(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(visibility == 0xF0000000, "ordinary glass queries do not shatter panes");
    mantle(nullptr, nullptr, trace.data(), overlap, overlap, bounds, 7, 16);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(visibility == 0x70000000, "native mantle overlap breaks just the touched pane");
    customglass::PumpEffects();
    Check(glassFxCalls == 2 && glassSoundCalls == 2,
          "mantle uses the same effect and audio path as shooting");
    customglass::Load(dir);
    using Physics = void (*)(int, void*, const float*, const float*, const float*, const int*, int,
                             int, int, int, const unsigned char*, int);
    auto physics = reinterpret_cast<Physics>(image + replay::PhysicsBulletTrace.rva);
    const float point[6]{};
    glassStockFraction = .1f;
    physics(1, trace.data(), rayA, rayB, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0);
    customglass::PumpEffects();
    Check(glassFxCalls == 2, "low-level bullet respects walls");
    glassStockFraction = 1;
    physics(1, trace.data(), rayA, rayB, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0);
    customglass::PumpEffects();
    Check(glassFxCalls == 3 && glassSoundCalls == 3,
          "low-level weapon query independently breaks panes with native effects");
    {
        std::ofstream f(dir / "glass.bin", std::ios::binary);
        f.write("MWRGLS02", 8);
        unsigned counts[]{1, 4, 4, 2};
        f.write(reinterpret_cast<char*>(counts), sizeof(counts));
        float data[]{1, 0, 0, 4, 0, -20, 0, 0, 20, 0, 0, 20, 80, 0, -20, 80};
        f.write(reinterpret_cast<char*>(data), sizeof(data));
        unsigned surfaces[]{0, 1};
        f.write(reinterpret_cast<char*>(surfaces), sizeof(surfaces));
    }
    customglass::Load(dir);
    glassStockFraction = .45f;
    physics(1, trace.data(), rayA, rayB, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0);
    customglass::PumpEffects();
    Check(glassFxCalls == 4 && glassSoundCalls == 4,
          "shot at authored pane thickness breaks before its center plane");
    visibility = 0xF0000000;
    customglass::HideBroken(reinterpret_cast<uintptr_t>(world.data()), 0);
    Check(visibility == 0x30000000, "all faces of a grouped window disappear together");
    glassStockFraction = 1;
    customglass::Clear();
    fs::remove(dir / "glass.bin");
    puts(
        "PASS: exact glass hook prologues and 7/8/11/12-argument ABIs; occluded shots, low-level weapon traces, pane thickness, whole-window visibility, shot/mantle native FX and spatial audio queue, no duplicate events, reload reset (engine traces and playback mocked)");
}
