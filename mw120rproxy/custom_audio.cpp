#include "custom_audio.h"
#include "custom_physics.h"
#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <atomic>
#include <cstring>
#include <array>

namespace {
uintptr_t base = 0;
using Pick = void (*)(const void*,
                      const void*,
                      const void**,
                      const void**,
                      float*,
                      unsigned,
                      unsigned,
                      unsigned,
                      unsigned);
using Grab = void (*)(int, int, bool, unsigned char);
std::atomic<Pick> pick{nullptr};
std::atomic<Grab> grab{nullptr};
std::atomic<unsigned> variants{0}, reports{0};
bool MovementName(const char* name) {
    return !strncmp(name, "step_", 5) || !strncmp(name, "footstep_", 9) ||
           !strncmp(name, "ladder_grab_", 12);
}
void PickAlias(const void* list,
               const void* params,
               const void** first,
               const void** second,
               float* blend,
               unsigned a,
               unsigned b,
               unsigned c,
               unsigned d) {
    pick.load()(list, params, first, second, blend, a, b, c, d);
    if (!customphysics::OwnsEmptyWorld() || !list || !first || *first)
        return;
    // Exact Replay SND_PickSoundAliasFromList: list +10 entries, +18 count;
    // entry stride E8, surface mask +40; play parameters +64 surface number.
    const char* name = nullptr;
    const unsigned char* entries = nullptr;
    int count = 0, surface = 0;
    char text[160]{};
    if (!safemem::ReadBytes(list, &name, 8) || !safemem::ReadString(name, text, sizeof(text)) ||
        !MovementName(text) ||
        !safemem::ReadBytes(static_cast<const char*>(list) + 0x10, &entries, 8) ||
        !safemem::ReadBytes(static_cast<const char*>(list) + 0x18, &count, 4) || count < 1 ||
        count > 4096 || !safemem::ReadBytes(static_cast<const char*>(params) + 0x64, &surface, 4))
        return;
    // Some imported surfaces have no compatible map sound-zone context. Only
    // after the native picker fails, use the bank's authored surface variants,
    // then concrete, then its untyped default. Keep native bank-owned pointers.
    const void* choices[64]{};
    unsigned used = 0;
    for (int pass = 0; pass < 3 && !used; ++pass) {
        const uint64_t mask =
            pass == 2 ? 0 : 1ull << (pass == 1 ? 5 : (surface > 0 && surface < 64 ? surface : 5));
        for (int i = 0; i < count && used < 64; ++i) {
            uint64_t bits = 0;
            const auto* entry = entries + size_t(i) * 0xE8;
            if (!safemem::ReadBytes(entry + 0x40, &bits, 8))
                return;
            if (mask ? (bits & mask) != 0 : bits == 0)
                choices[used++] = entry;
        }
    }
    if (!used)
        return;
    *first = choices[variants.fetch_add(1) % used];
    if (second)
        *second = nullptr;
    if (blend)
        *blend = 0;
    if (reports.fetch_add(1) < 16)
        LOG_INFO("MovementAudio", "native picker fallback alias=%s surface=%d variants=%u", text,
                 surface, used);
}
void LadderGrab(int client, int entity, bool left, unsigned char surface) {
    const bool custom = client == 0 && entity == 0 && customphysics::OwnsEmptyWorld();
    if (custom && !surface)
        surface = 22; // authored ladder fallback: solid wood
    grab.load()(client, entity, left, surface);
    if (!custom)
        return;
    // These caches are used by this exact Replay grab event. If resident, the
    // native call (and picker fallback above) already handled the sound.
    uintptr_t cached = 0;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + (left ? 0xF276578 : 0xF276580)), &cached,
                            8) ||
        cached)
        return;
    uintptr_t entities = 0, player = 0;
    std::array<float, 3> origin{};
    int pmType = -1;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0xBC20F00), &entities, 8) || !entities ||
        !safemem::ReadBytes(reinterpret_cast<void*>(entities + 0x150), &player, 8) || !player ||
        !safemem::ReadBytes(reinterpret_cast<void*>(player + 0xC), &pmType, 4) || pmType != 0 ||
        !safemem::ReadBytes(reinterpret_cast<void*>(player + 0x30), origin.data(), 12))
        return;
    const bool metal = surface == 13 || surface == 14 || surface == 28 || surface == 43;
    const char* name = metal ? (left ? "step_default_plr_walk_metal_ladder_left"
                                     : "step_default_plr_walk_metal_ladder_right")
                             : (left ? "step_default_plr_walk_wood_ladder_left"
                                     : "step_default_plr_walk_wood_ladder_right");
    auto* alias =
        reinterpret_cast<void* (*)(const char*)>(base + replay::SoundAliasByName.rva)(name);
    unsigned id = 0;
    if (alias)
        safemem::ReadBytes(static_cast<char*>(alias) + 8, &id, 4);
    if (id) {
        reinterpret_cast<void (*)(unsigned, int, int, const float*)>(
            base + replay::SoundAtPosition.rva)(id, client, entity, origin.data());
        if (reports.fetch_add(1) < 16)
            LOG_INFO("MovementAudio", "ladder grab fallback alias=%s", name);
    }
}
}
namespace customaudio {
hook::Status Install(uintptr_t address) {
    base = address;
    for (const auto* b : {&replay::SoundAliasByName, &replay::SoundAtPosition}) {
        unsigned char bytes[64]{};
        if (!safemem::ReadBytes(reinterpret_cast<void*>(base + b->rva), bytes, b->size) ||
            memcmp(bytes, b->bytes, b->size))
            return hook::Status::NotReady;
    }
    auto s = hook::Install(reinterpret_cast<void*>(base + replay::PickSoundAlias.rva), &PickAlias,
                           replay::PickSoundAlias.bytes, replay::PickSoundAlias.size, pick);
    if (s != hook::Status::Installed)
        return s;
    return hook::Install(reinterpret_cast<void*>(base + replay::LadderGrabSound.rva), &LadderGrab,
                         replay::LadderGrabSound.bytes, replay::LadderGrabSound.size, grab);
}
}
