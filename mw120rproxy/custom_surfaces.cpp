#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "inline_hook.h"
#include "safemem.h"

namespace {
// Exact Replay movement trace wrapper (CD24C0): both the short ground probes
// and segmented sweeps call PhysicsQuery at 1098D50, bypassing LegacyTrace.
using MovementTrace =
    void (*)(void*, void*, void*, const float*, const float*, const float*, int, int, int, bool);
std::atomic<MovementTrace> movementTrace{nullptr};
std::atomic<unsigned> movementSamples{0};
void Trace(void* handler,
           void* pm,
           void* result,
           const float* start,
           const float* end,
           const float* bounds,
           int pass,
           int mask,
           int flags,
           bool cheap) {
    movementTrace.load()(handler, pm, result, start, end, bounds, pass, mask, flags, cheap);
    if (!customphysics::OwnsEmptyWorld())
        return;
    customsurfaces::Apply(result, start, end);
    // Bounded evidence of the actual movement path, separate from the older
    // legacy traces which can occur during spawn without walking the floor.
    if (movementSamples.fetch_add(1) < 12) {
        float fraction = 0, normalZ = 0;
        unsigned surfaceFlags = 0;
        unsigned short entity = 0;
        auto* b = static_cast<unsigned char*>(result);
        memcpy(&fraction, b, 4);
        memcpy(&normalZ, b + replaytrace::NormalZ, 4);
        memcpy(&surfaceFlags, b + 0x1C, 4);
        memcpy(&entity, b + 0x2C, 2);
        LOG_INFO(
            "Footsteps",
            "movement trace fraction=%.3f normalZ=%.3f entity=%u surface=%u start=(%.1f %.1f %.1f)",
            fraction, normalZ, entity, (surfaceFlags >> 19) & 63, start[0], start[1], start[2]);
    }
}
}
namespace customsurfaces {
void ObserveMovement(const void* pm) {
    if (!pm || !customphysics::OwnsEmptyWorld())
        return;
    struct SampleState {
        unsigned epoch = 0, count = 0;
        ULONGLONG next = 0;
        std::array<float, 3> origin{};
    };
    static thread_local SampleState state;
    const unsigned epoch = movementEpoch.load();
    if (state.epoch != epoch)
        state = {epoch};
    const auto now = GetTickCount64();
    if (state.count >= 80 || now < state.next)
        return;
    state.next = now + 750;
    const auto* bytes = static_cast<const unsigned char*>(pm);
    const unsigned char *ps = nullptr, *ground = nullptr;
    std::array<float, 6> motion{};
    std::array<unsigned char, 0x4E> contact{};
    unsigned short entity = 2047;
    unsigned long long flags = 0;
    if (!safemem::ReadBytes(bytes + 8, &ps, sizeof(ps)) || !ps ||
        !safemem::ReadBytes(bytes + 0x368, &ground, sizeof(ground)) || !ground ||
        !safemem::ReadBytes(ps + 0x30, motion.data(), sizeof(motion)) ||
        !safemem::ReadBytes(ps + 0x272, &entity, sizeof(entity)) ||
        !safemem::ReadBytes(ps + 0x14, &flags, sizeof(flags)) ||
        !safemem::ReadBytes(ground, contact.data(), contact.size()))
        return;
    float displacement = 0, speedSquared = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const float delta = motion[k] - state.origin[k];
        displacement += delta * delta;
        speedSquared += motion[k + 3] * motion[k + 3];
        state.origin[k] = motion[k];
    }
    if (state.count >= 4 && displacement < .01f && speedSquared < 1)
        return;
    float fraction = 0, normalZ = 0;
    unsigned surface = 0, hitType = 0;
    memcpy(&fraction, contact.data(), 4);
    memcpy(&normalZ, contact.data() + replaytrace::NormalZ, 4);
    memcpy(&surface, contact.data() + 0x1C, 4);
    memcpy(&hitType, contact.data() + 0x24, 4);
    ++state.count;
    LOG_INFO("Movement",
             "sample=%u origin=(%.2f %.2f %.2f) velocity=(%.2f %.2f %.2f) ground=%u walking=%u plane=%u fraction=%.4f normalZ=%.3f surfaceFlags=%X hitType=%u pmFlags=%llX",
             state.count, motion[0], motion[1], motion[2], motion[3], motion[4], motion[5],
             entity, contact[0x4C], contact[0x4D], fraction, normalZ, surface, hitType, flags);
}
hook::Status Install(uintptr_t base) {
    const auto& b = replay::MovementTrace;
    return hook::Install(reinterpret_cast<void*>(base + b.rva), &Trace, b.bytes, b.size,
                         movementTrace);
}
}
