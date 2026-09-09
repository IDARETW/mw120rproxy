struct SharedShape {
    unsigned refs = 1;
    std::vector<SharedShape*> children;
};
unsigned sharedConstructed = 0, sharedFreed = 0, sharedInstances = 0, sharedDestroyed = 0;
std::vector<SharedShape*> sharedBodies[5];
std::vector<unsigned> sharedContents[5];
void SharedRelease(void* pointer) {
    auto* shape = static_cast<SharedShape*>(pointer);
    Check(shape->refs > 0, "live shared shape reference");
    if (--shape->refs == 0) {
        for (auto* child : shape->children)
            SharedRelease(child);
        delete shape;
        ++sharedFreed;
    }
}
void* SharedBox(const float*, const float*) {
    ++sharedConstructed;
    return new SharedShape;
}
void* SharedCompound(compoundcollision::Array* array) {
    auto* shape = new SharedShape;
    ++sharedConstructed;
    for (int i = 0; i < array->size; ++i) {
        auto* child = static_cast<SharedShape*>(array->data[i].shape);
        ++child->refs;
        shape->children.push_back(child);
    }
    return shape;
}
unsigned SharedInstantiate(int world,
                           const void* pointer,
                           int ref,
                           const char*,
                           const char*,
                           int contents,
                           const float*,
                           const float*,
                           bool a,
                           bool b,
                           bool c) {
    Check(ref == 0 && contents && !(contents & ~collisionfile::SupportedContents) && a && b && !c,
          "shared shape instantiation retains native body ABI");
    auto* shape = (SharedShape*)pointer;
    ++shape->refs;
    sharedBodies[world].push_back(shape);
    sharedContents[world].push_back(unsigned(contents));
    ++sharedInstances;
    return unsigned(sharedBodies[world].size() - 1);
}
void SharedDestroy(int world, unsigned index, bool deferred) {
    Check(!deferred, "synchronous shared body release");
    SharedRelease(sharedBodies[world][index]);
    ++sharedDestroyed;
}
uintptr_t SharedWorld(int world) {
    return 100 + world;
}
const unsigned char* BulletPriority(const void*, bool) {
    return nullptr;
}
int BulletSkipCount(void*) {
    return 0;
}
void PrepareNativeBullet() {
    // Keep both installed low-level query hooks. Only the external Havok trace
    // and weapon metadata lookup are mocked; execute the full native consumer.
    NativeBytes(0x11D5390, ReplayBulletCode);
    Commit(0x173CD20);
    NativeBytes(0x173CD20, ReplayClientBulletCode);
    Commit(0x24C5260);
    for (uintptr_t rva : {uintptr_t{0x1138090}, uintptr_t{0x1169170}, ReplayTraceHitIdRva,
                          uintptr_t{0x24C5260}, uintptr_t{0xEE55C98}})
        Commit(rva);
    Jump(image + 0x1138090, reinterpret_cast<void*>(&BulletPriority));
    Jump(image + 0x1169170, reinterpret_cast<void*>(&BulletSkipCount));
    NativeBytes(ReplayTraceHitIdRva, ReplayTraceHitIdCode);
    static unsigned char dvar[64]{};
    auto* ptr = dvar;
    memcpy(image + 0xEE55C98, &ptr, 8);
    memset(image + 0x24C5260, 0, 24);
    FlushInstructionCache(GetCurrentProcess(), image, 0x1324B000);
}
void NativeClientBulletResult(bool expected) {
    alignas(16) unsigned char bp[0x90]{}, br[0x80];
    memset(br, 0xA5, sizeof(br));
    const float start[]{-64, 16, 16}, end[]{64, 16, 16};
    memcpy(bp + 0x68, start, 12);
    memcpy(bp + 0x74, end, 12);
    using ClientBullet =
        bool (*)(int, void*, const void*, bool, const void*, void*, int, const void**, bool);
    const void* entity = reinterpret_cast<void*>(0x1234);
    glassExpectedSkip = reinterpret_cast<int*>(bp + 4);
    const bool result = reinterpret_cast<ClientBullet>(image + 0x173CD20)(
        0, bp, nullptr, false, nullptr, br, 0, &entity, false);
    glassExpectedSkip = nullptr;
    Check(result == expected, "actual client bullet routine must return the corrected world hit");
    if (expected) {
        float fraction, impact, distance;
        unsigned depth;
        memcpy(&fraction, br, 4);
        memcpy(&impact, br + 0x50, 4);
        memcpy(&distance, bp + 0x8C, 4);
        memcpy(&depth, br + 0x60, 4);
        Check(fraction == .5f && impact == 0 && distance == 64 && depth == 5 && !entity &&
                  !br[0x5C],
              "client impact position, distance, material and world-entity result reach effects");
    }
    Check(br[0x48] == 0xA5 && br[0x64] == 0xA5,
          "client query preserves fields outside its native trace and output writes");
}
bool NativeBulletResult(bool expected, bool legacy = false) {
    auto** dvar = reinterpret_cast<unsigned char**>(image + 0xEE55C98);
    (*dvar)[0x28] = legacy;
    alignas(16) unsigned char bp[0x90]{}, br[0x80];
    memset(br, 0xA5, sizeof(br));
    const float start[]{-64, 16, 16}, end[]{64, 16, 16};
    memcpy(bp + 0x68, start, 12);
    memcpy(bp + 0x74, end, 12);
    using Bullet = bool (*)(void*, const void*, bool, void*, void*, int, bool);
    glassExpectedSkip = reinterpret_cast<int*>(bp + 4);
    const auto result =
        reinterpret_cast<Bullet>(image + 0x11D5390)(bp, nullptr, false, nullptr, br, 0, false);
    glassExpectedSkip = nullptr;
    Check(result == expected, "native bullet consumer accepts corrected hit and rejects miss");
    if (expected) {
        float fraction, impact, distance;
        unsigned depth;
        void* entity;
        memcpy(&fraction, br, 4);
        memcpy(&impact, br + 0x50, 4);
        memcpy(&distance, bp + 0x8C, 4);
        memcpy(&depth, br + 0x60, 4);
        memcpy(&entity, br + 0x48, 8);
        Check(fraction == .5f && impact == 0 && distance == 64 && depth == 5 && !entity,
              "native bullet computes wall impact, travel distance and concrete penetration type");
    }
    Check(br[0x64] == 0xA5, "trace correction does not overwrite BulletTraceResults tail");
    return result;
}
void CollisionShotTests() {
    // This test follows the hook-installed call chain used by real bullets.
    NativeBulletResult(true);
    NativeBulletResult(true, true);
    NativeClientBulletResult(true);
    glassStockFraction = .5f;
    NativeBulletResult(true);
    NativeClientBulletResult(true);
    glassStockFraction = 1;
    puts(
        "PASS: exact Replay bullet consumers accept custom misses and same-plane anonymous Havok hits as world geometry");
    const float start[]{-64, 16, 16}, end[]{64, 16, 16}, point[6]{};
    unsigned char trace[0x48]{};
    auto reset = [&] {
        memset(trace, 0, sizeof(trace));
        const float miss = 1;
        memcpy(trace, &miss, 4);
    };
    for (int world = 0; world < 5; ++world) {
        reset();
        customcollision::TraceShot(world, trace, start, end, point, 1, 0, true);
        Check(*reinterpret_cast<float*>(trace) == .5f, "bullet hulls available in all five worlds");
    }
    reset();
    const float nearFraction = .25f;
    memcpy(trace, &nearFraction, 4);
    trace[0x24] = 1;
    trace[0x2C] = 7;
    auto original = std::array<unsigned char, 0x48>{};
    memcpy(original.data(), trace, sizeof(trace));
    customcollision::TraceShot(1, trace, start, end, point, 1, 0, true);
    Check(!memcmp(original.data(), trace, sizeof(trace)),
          "nearer entity trace remains byte-identical");
    reset();
    customcollision::TraceShot(1, trace, start, end, point, 0x10, 0, true);
    Check(*reinterpret_cast<float*>(trace) == 1, "solid-excluding mask preserved");
    customcollision::TraceShot(1, trace, start, end, point, 1, 1, true);
    Check(*reinterpret_cast<float*>(trace) == 1, "non-All phase preserved");
    const float swept[]{0, 0, 0, 15, 15, 30};
    customcollision::TraceShot(1, trace, start, end, swept, 1, 0, true);
    Check(*reinterpret_cast<float*>(trace) == 1, "player sweep remains native");
    reset();
    const float inside[]{16, 16, 16};
    customcollision::TraceShot(1, trace, inside, end, point, 1, 0, true);
    Check(*reinterpret_cast<float*>(trace) == 1 && !trace[0x3D] && !trace[0x3C] && trace[0x24] == 0,
          "inside-only software hull does not fabricate a zero-distance weapon impact");
    using ClientPhysics = void (*)(int, void*, const float*, const float*, const float*, const int*,
                                   int, int, int, int, const unsigned char*, int, bool);
    auto clientPhysics =
        reinterpret_cast<ClientPhysics>(image + replay::PhysicsClientBulletTrace.rva);
    clientExpectedInside = false;
    clientPhysics(4, trace, inside, end, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0, false);
    Check(*reinterpret_cast<float*>(trace) == 1, "client preserves disabled inside-ray detection");
    clientExpectedInside = true;
    clientPhysics(4, trace, inside, end, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0, true);
    Check(
        *reinterpret_cast<float*>(trace) == 1 && !trace[0x3D],
        "client inside ray does not replace a useful native penetration trace with fraction zero");
    clientExpectedPhase = 1;
    clientPhysics(4, trace, start, end, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 1, true);
    Check(*reinterpret_cast<float*>(trace) == 1, "unrelated client phase remains native");
    clientExpectedPhase = 0;
    clientPhysics(4, trace, start, end, swept, nullptr, 0, 0, 0x2806931, 1, nullptr, 0, true);
    Check(*reinterpret_cast<float*>(trace) == 1, "client sweep remains native");
    glassStockFraction = .25f;
    clientPhysics(4, trace, start, end, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0, true);
    Check(*reinterpret_cast<float*>(trace) == .25f, "closer client hit remains native");
    glassStockFraction = 1;
    const auto glassPath = packageRoot / id / "glass.bin";
    {
        std::ofstream f(glassPath, std::ios::binary);
        f.write("MWRGLS01", 8);
        unsigned counts[]{1, 1, 4, 1};
        f.write(reinterpret_cast<char*>(counts), sizeof(counts));
        float data[]{1, 0, 0, 80, 0, 0, 80, 32, 0, 80, 32, 32, 80, 0, 32};
        f.write(reinterpret_cast<char*>(data), sizeof(data));
        unsigned surface = 0;
        f.write(reinterpret_cast<char*>(&surface), 4);
    }
    customglass::Load(glassPath.parent_path());
    std::vector<unsigned char> gfxWorld(0x4300);
    unsigned surfaces = 1, visibility = 0x80000000;
    auto* visible = &visibility;
    memcpy(gfxWorld.data() + 0xC8, &surfaces, 4);
    memcpy(gfxWorld.data() + 0x40B8, &visible, 8);
    using Physics = void (*)(int, void*, const float*, const float*, const float*, const int*, int,
                             int, int, int, const unsigned char*, int);
    auto physics = reinterpret_cast<Physics>(image + replay::PhysicsBulletTrace.rva);
    const float behindPane[]{128, 16, 16}, clearStart[]{40, 16, 16};
    physics(1, trace, start, behindPane, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(gfxWorld.data()), 0);
    Check(std::abs(*reinterpret_cast<float*>(trace) - 1.f / 3) < 1e-6f && visibility == 0x80000000,
          "corrected world hull prevents a shot from breaking glass behind the wall");
    physics(1, trace, clearStart, behindPane, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(gfxWorld.data()), 0);
    Check(visibility == 0, "unoccluded shot still breaks glass after world correction");
    customglass::Load(glassPath.parent_path());
    visibility = 0x80000000;
    clientPhysics(4, trace, start, behindPane, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0,
                  true);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(gfxWorld.data()), 0);
    Check(visibility == 0x80000000, "client bullet also respects glass behind corrected walls");
    clientPhysics(4, trace, clearStart, behindPane, point, nullptr, 0, 0, 0x2806931, 1, nullptr, 0,
                  true);
    customglass::HideBroken(reinterpret_cast<uintptr_t>(gfxWorld.data()), 0);
    Check(visibility == 0,
          "unoccluded client shot shatters glass through the existing debris path");
    customglass::Clear();
    fs::remove(glassPath);
    puts(
        "PASS: exact Replay bullet consumer reproduces miss before collision load, then accepts both query paths; impact position, distance, penetration type, five worlds, native nearer hits, filters and movement isolation");
    puts("PASS: installed bullet hook combines convex world occlusion and breakable glass");
}
void SharedCollisionTests() {
    const auto dir = packageRoot / id;
    {
        std::ofstream f(dir / "collision.bin", std::ios::binary);
        f.write("MWCOLL01", 8);
        unsigned count = 513;
        f.write((char*)&count, 4);
        float box[]{0, 0, 0, 32, 32, 32};
        for (unsigned i = 0; i < count; ++i)
            f.write((char*)box, 24);
    }
    for (const auto* b : {&replay::CreateShapeAabb, &replay::CreateShapeConvex,
                          &replay::CreateShapeCompound, &replay::InstantiateStaticBody,
                          &replay::DestroyPhysicsInstance, &replay::RemoveHavokReference}) {
        Commit(b->rva);
        memcpy(image + b->rva, b->bytes, b->size);
    }
    Commit(replay::WorldCollisionCreate.rva);
    Commit(replay::WorldCollisionShutdown.rva);
    PrologueFixture(replay::WorldCollisionCreate, {0x41, 0x5D, 0x41, 0x5C, 0x5F},
                    reinterpret_cast<void*>(&SharedWorld));
    PrologueFixture(replay::WorldCollisionShutdown,
                    {0x48, 0x8B, 0x7C, 0x24, 0x48, 0x48, 0x83, 0xC4, 0x28, 0x5D, 0x5B},
                    reinterpret_cast<void*>(&SharedWorld));
    Check(customcollision::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "shared collision hooks validate native prologues");
    Stub(replay::CreateShapeAabb, &SharedBox);
    Stub(replay::CreateShapeCompound, &SharedCompound);
    Stub(replay::InstantiateStaticBody, &SharedInstantiate);
    Stub(replay::DestroyPhysicsInstance, &SharedDestroy);
    Stub(replay::RemoveHavokReference, &SharedRelease);
    *reinterpret_cast<uintptr_t*>(image + 0xE5C62F8) = 0;
    auto create = reinterpret_cast<uintptr_t (*)(int)>(image + replay::WorldCollisionCreate.rva);
    auto shutdown =
        reinterpret_cast<uintptr_t (*)(int)>(image + replay::WorldCollisionShutdown.rva);
    PrepareNativeBullet();
    NativeBulletResult(false);
    NativeClientBulletResult(false);
    for (int world : {0, 1})
        Check(create(world) == 100 + world, "original world creation result preserved");
    Check(sharedConstructed == 516 && sharedInstances == 6 && sharedFreed == 0,
          "server worlds share 513 child shapes and three compounds");
    CollisionShotTests();
    for (int world : {2, 3, 4})
        Check(create(world) == 100 + world, "remaining world creation result preserved");
    Check(sharedConstructed == 516 && sharedInstances == 15 && sharedFreed == 0,
          "late client worlds reuse the server-created collision shapes");
    for (int world : {2, 0, 4, 1})
        shutdown(world);
    Check(sharedFreed == 0 && sharedDestroyed == 12,
          "out-of-order shutdown retains shapes for remaining world");
    shutdown(3);
    NativeBulletResult(false);
    NativeClientBulletResult(false);
    Check(sharedFreed == 516 && sharedDestroyed == 15,
          "last world releases every constructor and body reference exactly once");
    for (auto& bodies : sharedBodies)
        bodies.clear();
    create(0);
    shutdown(0);
    Check(sharedConstructed == 1032 && sharedFreed == 1032,
          "subsequent map load creates fresh shapes without stale references");
    for (int world = 0; world < 5; ++world) {
        sharedBodies[world].clear();
        sharedContents[world].clear();
    }
    {
        std::ofstream f(dir / "collision.bin", std::ios::binary);
        f.write("MWCOLL03", 8);
        const unsigned count = 513, vertices = 8;
        f.write(reinterpret_cast<const char*>(&count), 4);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned contents = i % 3 == 0   ? collisionfile::Solid
                                      : i % 3 == 1 ? collisionfile::PlayerClip
                                                   : collisionfile::ShotClip;
            f.write(reinterpret_cast<const char*>(&vertices), 4);
            f.write(reinterpret_cast<const char*>(&contents), 4);
            for (float x : {0.f, 32.f})
                for (float y : {0.f, 32.f})
                    for (float z : {0.f, 32.f}) {
                        const float vertex[]{x, y, z};
                        f.write(reinterpret_cast<const char*>(vertex), 12);
                    }
        }
    }
    create(0);
    create(4);
    const std::vector<unsigned> expectedContents{collisionfile::Solid, collisionfile::ShotClip,
                                                 collisionfile::PlayerClip};
    Check(sharedContents[0] == expectedContents && sharedContents[4] == expectedContents,
          "typed native compounds preserve per-body filters across shared worlds");
    alignas(16) unsigned char typedTrace[0x48]{};
    const float start[]{-32, 16, 16}, end[]{64, 16, 16}, bounds[6]{};
    *reinterpret_cast<float*>(typedTrace) = 1;
    customcollision::TraceShot(4, typedTrace, start, end, bounds, collisionfile::ShotClip, 0, true);
    unsigned hitContents;
    memcpy(&hitContents, typedTrace + 0x20, 4);
    Check(hitContents == collisionfile::ShotClip &&
              std::abs(*reinterpret_cast<float*>(typedTrace) - 1.f / 3) < .00001f,
          "runtime bullet fallback respects query mask and writes actual source contents");
    shutdown(0);
    shutdown(4);
    Check(sharedConstructed == 1548 && sharedFreed == 1548,
          "typed shape references release without leaks or duplicate construction");
    fs::remove(dir / "collision.bin");
    puts(
        "PASS: software bullet traces reach all worlds before late client body creation; exact native hit decoder, consumer ABI, shared shapes, shutdown and reload verified");
}
