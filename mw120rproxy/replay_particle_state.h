#pragma once
#include "safemem.h"
#include <array>

namespace replayparticle {
struct Emitter {
    unsigned flags = 0;
    unsigned particles = 0;
};
struct Snapshot {
    bool present = false;
    bool running = false;
    unsigned long long flags = 0;
    unsigned emitterCount = 0;
    std::array<Emitter, 8> emitters{};
};
template <typename T> bool Read(uintptr_t address, T& value) {
    return safemem::ReadBytes(reinterpret_cast<const void*>(address), &value, sizeof(value));
}
inline bool
Inspect(uintptr_t base, unsigned handle, uintptr_t expectedDefinition, Snapshot& result) {
    result = {};
    if (!handle || !expectedDefinition)
        return false;
    // Replay FX_PlayEffect 1819D34..1819D46 and ParticleSystem::Init 1BCA690.
    uintptr_t system = 0;
    if (!Read(base + 0x12C5BA00 + 8 * (handle & 0xFFFu), system))
        return false;
    if (system < 0x10000)
        return true;
    unsigned currentHandle = 0;
    uintptr_t definition = 0, emitters = 0;
    if (!Read(system + 0x1A8, currentHandle) || !Read(system + 0x190, definition))
        return false;
    if (currentHandle != handle || definition != expectedDefinition)
        return true;
    unsigned char running = 0;
    if (!Read(system + 0x198, emitters) || !Read(system + 0x1A0, result.flags) ||
        !Read(system + 0x22F, running) || !Read(definition + 0x1C, result.emitterCount) ||
        result.emitterCount > result.emitters.size())
        return false;
    result.present = true;
    result.running = running != 0;
    uintptr_t emitterDefinitions = 0;
    if (!Read(definition + 8, emitterDefinitions))
        return false;
    for (unsigned i = 0; i < result.emitterCount; ++i) {
        const uintptr_t emitter = emitters + i * 0x200;
        auto& report = result.emitters[i];
        if (!Read(emitter + 0x170, report.flags))
            return false;
        if (report.flags & 0x40)
            continue;
        uintptr_t states = 0;
        unsigned count = 0;
        if (!Read(emitter + 0x158, states) || !Read(emitterDefinitions + i * 0xA0 + 8, count) ||
            count > 16)
            return false;
        // Replay ParticleEmitter::GetParticleCount 1BAA210 sums both queues.
        for (unsigned j = 0; j < count; ++j) {
            unsigned active = 0, delayed = 0;
            if (!Read(states + j * 0x200 + 0x1A8, active) ||
                !Read(states + j * 0x200 + 0x1AC, delayed) || active > 65536 || delayed > 65536)
                return false;
            report.particles += active + delayed;
        }
    }
    // Discard a snapshot if the engine recycled this handle during the reads.
    uintptr_t currentSystem = 0;
    if (!Read(base + 0x12C5BA00 + 8 * (handle & 0xFFFu), currentSystem) ||
        !Read(system + 0x1A8, currentHandle) || !Read(system + 0x190, definition) ||
        currentSystem != system || currentHandle != handle || definition != expectedDefinition) {
        result = {};
        return false;
    }
    return true;
}
}
