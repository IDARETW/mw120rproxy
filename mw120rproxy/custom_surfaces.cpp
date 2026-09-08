#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "inline_hook.h"

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

    if (movementSamples.fetch_add(1) < 12) {
        float fraction = 0, normalZ = 0;
        unsigned surfaceFlags = 0;
        unsigned short entity = 0;
        auto* b = static_cast<unsigned char*>(result);
        memcpy(&fraction, b, 4);
        memcpy(&normalZ, b + 12, 4);
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
hook::Status Install(uintptr_t base) {
    const auto& b = replay::MovementTrace;
    return hook::Install(reinterpret_cast<void*>(base + b.rva), &Trace, b.bytes, b.size,
                         movementTrace);
}
}
