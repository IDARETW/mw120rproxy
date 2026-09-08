#include "custom_ladders.h"
#include "ladder_file.h"
#include "custom_physics.h"
#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "logger.h"
#include <atomic>
#include <memory>
#include <intrin.h>
#include <algorithm>

namespace {
// Replay CC4470: axis +0, bottom +24h, top +30h, width +3Ch, rung distance +40h.
struct Info {
    float axis[3][3], bottom[3], top[3], width, rungDistance;
};
static_assert(sizeof(Info) == 68);
using GetInfo = bool (*)(const float*, const void*, Info*, float*, bool, unsigned*, unsigned*);
using Trace =
    void (*)(void*, void*, void*, const float*, const float*, const float*, int, int, bool);
std::atomic<GetInfo> g_info{nullptr};
std::atomic<Trace> g_trace{nullptr};
using CheckMove = void (*)(void*, void*);
std::atomic<CheckMove> g_check{nullptr};
std::atomic<std::shared_ptr<const std::vector<ladderfile::Face>>> g_faces;
uintptr_t g_base = 0;
std::atomic<unsigned> g_hits{0};
std::atomic<unsigned> g_checks{0}, g_traceSamples{0};
float Distance(const ladderfile::Face& f, const float* p) {
    return (p[0] - f.bottom[0]) * f.normal[0] + (p[1] - f.bottom[1]) * f.normal[1];
}
float Lateral(const ladderfile::Face& f, const float* p) {
    return -(p[0] - f.bottom[0]) * f.normal[1] + (p[1] - f.bottom[1]) * f.normal[0];
}
void Check(void* pm, void* pml) {
    const auto faces = g_faces.load();
    auto* bytes = static_cast<unsigned char*>(pm);
    if (faces && !faces->empty() && customphysics::OwnsEmptyWorld()) {
        unsigned char* ps = nullptr;
        memcpy(&ps, bytes + 8, 8);
        const auto* origin = reinterpret_cast<const float*>(ps + 0x30);
        for (const auto& f : *faces) {
            const float distance = Distance(f, origin);
            if (distance < -.5f || distance > 72 ||
                std::abs(Lateral(f, origin)) > f.width * .5f + 20 || origin[2] < f.bottom[2] - 24 ||
                origin[2] > f.top[2] + 48)
                continue;
            // Replay's normal client sets this command bit from edge queries.
            // Imported ladders have authored faces instead of native edge data.
            auto* buttons = reinterpret_cast<unsigned long long*>(bytes + 0x10);
            const auto original = *buttons;
            *buttons |= 0x0800000000000000ull;
            if (g_checks.fetch_add(1) < 6)
                LOG_INFO("Ladders",
                         "armed native movement check origin=(%.1f %.1f %.1f) buttons=%llX",
                         origin[0], origin[1], origin[2], original);
            g_check.load()(pm, pml);
            *buttons = original;
            return;
        }
    }
    g_check.load()(pm, pml);
}
bool Get(const float* origin,
         const void* handler,
         Info* info,
         float* center,
         bool warning,
         unsigned* hint,
         unsigned* widthHint) {
    const auto faces = g_faces.load();
    if (faces && customphysics::OwnsEmptyWorld()) {
        const ladderfile::Face* best = nullptr;
        float score = 1e10f;
        for (const auto& f : *faces) {
            const float distance = Distance(f, origin), side = Lateral(f, origin);
            if (distance < -.5f || distance > 72 || std::abs(side) > f.width * .5f + 20 ||
                origin[2] < f.bottom[2] - 24 || origin[2] > f.top[2] + 48)
                continue;
            const float candidate = distance * distance + side * side;
            if (candidate < score) {
                score = candidate;
                best = &f;
            }
        }
        if (best) {
            *info = {};
            memcpy(info->axis[0], best->normal, 12);
            info->axis[1][0] = -best->normal[1];
            info->axis[1][1] = best->normal[0];
            info->axis[2][2] = 1;
            memcpy(info->bottom, best->grip, 12);
            memcpy(info->top, best->grip, 12);
            info->rungDistance = best->rungDistance;
            info->width = best->gripWidth;
            info->top[2] +=
                std::ceil((best->top[2] - best->grip[2]) / best->rungDistance) * best->rungDistance;
            if (center) {
                const float side = Lateral(*best, origin);
                for (unsigned k = 0; k < 3; ++k)
                    center[k] = -side * info->axis[1][k];
            }
            return true;
        }
    }
    return g_info.load()(origin, handler, info, center, warning, hint, widthHint);
}
void PlayerTrace(void* self,
                 void* pm,
                 void* result,
                 const float* start,
                 const float* end,
                 const float* bounds,
                 int pass,
                 int mask,
                 bool cheap) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
    g_trace.load()(self, pm, result, start, end, bounds, pass, mask, cheap);
    customsurfaces::Apply(result, start, end);
    // Only the two PM_CheckLadderMove tests consume the authored surface bit.
    if (caller != 0xCC63B6 && caller != 0xCC64E2)
        return;
    const auto faces = g_faces.load();
    if (!faces || !customphysics::OwnsEmptyWorld())
        return;
    float fraction;
    memcpy(&fraction, result, 4);
    if (g_checks.load() && g_traceSamples.fetch_add(1) < 8)
        LOG_INFO(
            "Ladders",
            "trace caller=%llX fraction=%.3f start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) bounds=(%.1f %.1f %.1f %.1f %.1f %.1f)",
            caller, fraction, start[0], start[1], start[2], end[0], end[1], end[2], bounds[0],
            bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]);
    if (!std::isfinite(fraction) || fraction < 0 || fraction >= 1)
        return;
    float hit[3];
    for (unsigned k = 0; k < 3; ++k)
        hit[k] = start[k] + fraction * (end[k] - start[k]) + bounds[k];
    for (const auto& f : *faces) {
        const float extent = std::abs(f.normal[0]) * bounds[3] + std::abs(f.normal[1]) * bounds[4];
        if (Distance(f, end) >= Distance(f, start) || Distance(f, start) < 0 ||
            std::abs(Distance(f, hit) - extent) > 2 || std::abs(Lateral(f, hit)) > f.width * .5f ||
            hit[2] + bounds[5] < f.bottom[2] || hit[2] - bounds[5] > f.top[2])
            continue;
        auto* flags = reinterpret_cast<unsigned char*>(result) + 0x1C;
        *flags |= 8;
        if (g_hits.fetch_add(1) < 4)
            LOG_INFO("Ladders", "native climb trace matched authored face at %.1f %.1f %.1f",
                     f.bottom[0], f.bottom[1], f.bottom[2]);
        break;
    }
}
}
namespace customladders {
void Load(const std::filesystem::path& directory) {
    auto faces = std::make_shared<std::vector<ladderfile::Face>>();
    const auto path = directory / "ladders.bin";
    if (std::filesystem::exists(path) && !ladderfile::Load(path, *faces))
        LOG_ERR("Ladders", "invalid ladders.bin; ladder support disabled for package");
    LOG_INFO("Ladders", "loaded %zu authored ladder faces", faces->size());
    g_faces.store(std::move(faces));
    g_hits = 0;
    g_checks = 0;
    g_traceSamples = 0;
}
hook::Status Install(uintptr_t base) {
    g_base = base;
    auto status = hook::Install(reinterpret_cast<void*>(base + replay::GetLadderInfo.rva), &Get,
                                replay::GetLadderInfo.bytes, replay::GetLadderInfo.size, g_info);
    if (status != hook::Status::Installed)
        return status;
    status =
        hook::Install(reinterpret_cast<void*>(base + replay::LegacyPlayerTrace.rva), &PlayerTrace,
                      replay::LegacyPlayerTrace.bytes, replay::LegacyPlayerTrace.size, g_trace);
    if (status != hook::Status::Installed)
        return status;
    return hook::Install(reinterpret_cast<void*>(base + replay::CheckLadderMove.rva), &Check,
                         replay::CheckLadderMove.bytes, replay::CheckLadderMove.size, g_check);
}
}
