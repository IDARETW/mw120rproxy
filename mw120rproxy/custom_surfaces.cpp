#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "inline_hook.h"
#include <mutex>
#include <climits>

namespace {
// Exact Replay movement trace wrapper (CD24C0): both the short ground probes
// and segmented sweeps call PhysicsQuery at 1098D50, bypassing LegacyTrace.
using MovementTrace =
    void (*)(void*, void*, void*, const float*, const float*, const float*, int, int, int, bool);
std::atomic<MovementTrace> movementTrace{nullptr};
struct MovementSample {
    int commandTime = 0;
    short animationSpeed = 0;
    unsigned cycle[2]{};
    unsigned movementTimer = 0, movementAnimation = 0;
    unsigned secondaryTimer = 0, secondaryAnimation = 0;
    unsigned long long otherFlags = 0;
    unsigned short ground = 2047;
    float velocity[3]{};
};
std::mutex reportMutex;
MovementSample pendingReport;
bool hasReport = false;
unsigned reports = 0;
std::atomic<ULONGLONG> nextReport{0};
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
}
}
namespace customsurfaces {
void ResetMovementReport() {
    std::lock_guard lock(reportMutex);
    reports = 0;
    hasReport = false;
    nextReport.store(0, std::memory_order_relaxed);
}
void CaptureMovement(const void* pm) {
    if (!pm || !customphysics::OwnsCustomWorld())
        return;
    const auto now = GetTickCount64();
    if (now < nextReport.load(std::memory_order_relaxed))
        return;
    std::unique_lock lock(reportMutex, std::try_to_lock);
    if (!lock || now < nextReport.load(std::memory_order_relaxed))
        return;
    if (reports >= 60) {
        nextReport.store(ULLONG_MAX, std::memory_order_relaxed);
        return;
    }
    nextReport.store(now + 2000, std::memory_order_relaxed);
    const unsigned char* ps = nullptr;
    memcpy(&ps, static_cast<const unsigned char*>(pm) + 8, sizeof(ps));
    if (!ps)
        return;
    // Capture the previous completed command on its simulation thread. The
    // client overlay writes the bounded report, never the movement callback.
    MovementSample sample;
    memcpy(&sample.commandTime, ps + 4, 4);
    memcpy(sample.velocity, ps + 0x3C, sizeof(sample.velocity));
    memcpy(&sample.ground, ps + 0x272, 2);
    memcpy(&sample.otherFlags, ps + 0x1C, 8);
    memcpy(sample.cycle, ps + 0x28, sizeof(sample.cycle));
    // Replay UpdateTimersAndEventsSlot CB76D1 reads the timer at E0 + slot*8;
    // TickPS CB6FD0/CB6FF0 tests the animation at E4 + slot*8.
    memcpy(&sample.movementTimer, ps + 0xE0, 4);
    memcpy(&sample.movementAnimation, ps + 0xE4, 4);
    memcpy(&sample.secondaryTimer, ps + 0xE8, 4);
    memcpy(&sample.secondaryAnimation, ps + 0xEC, 4);
    // Replay BgPlayer_Asm::TickPS CB6F69..CB6F77 stores the native speed here.
    memcpy(&sample.animationSpeed, ps + 0x1146, 2);
    pendingReport = sample;
    hasReport = true;
    ++reports;
}
void PumpMovementReport() {
    MovementSample sample;
    {
        std::unique_lock lock(reportMutex, std::try_to_lock);
        if (!lock || !hasReport)
            return;
        sample = pendingReport;
        hasReport = false;
    }
    LOG_INFO(
        "Movement",
        "native command=%d velocity=(%.2f %.2f %.2f) ground=%u animSpeed=%d bob=%08X/%08X movement=%08X timer=%u secondary=%08X timer=%u otherFlags=%llX",
        sample.commandTime, sample.velocity[0], sample.velocity[1], sample.velocity[2],
        sample.ground, sample.animationSpeed, sample.cycle[0], sample.cycle[1],
        sample.movementAnimation, sample.movementTimer, sample.secondaryAnimation,
        sample.secondaryTimer, sample.otherFlags);
}
hook::Status Install(uintptr_t base) {
    const auto& b = replay::MovementTrace;
    return hook::Install(reinterpret_cast<void*>(base + b.rva), &Trace, b.bytes, b.size,
                         movementTrace);
}
}
