#include "custom_doors.h"
#include "custom_collision.h"
#include "custom_glass.h"
#include "glass_file.h"
#include "custom_physics.h"
#include "custom_surfaces.h"
#include "replay_bindings.h"
#include "replay_particle_state.h"
#include "logger.h"
#include "safemem.h"
#include <atomic>
#include <memory>
#include <intrin.h>
#include <mutex>
namespace {
struct Event {
    glassfile::Vec origin, normal;
    unsigned pane;
};
struct State {
    std::vector<glassfile::Pane> panes;
    unsigned surfaces = 0;
    std::atomic<unsigned> broken[1024]{};
    std::mutex mutex;
    std::vector<Event> events;
    struct Observation {
        unsigned handle = 0, pane = 0, stage = 0;
        uintptr_t definition = 0;
        ULONGLONG due = 0;
    };
    std::vector<Observation> observations;
    unsigned observationCount = 0;
};
std::atomic<std::shared_ptr<State>> g_state;
uintptr_t g_base = 0;
using Bullet = bool (*)(void*, const void*, bool, void*, void*, int, bool);
using SlideTrace = void (*)(void*,
                            void*,
                            void*,
                            const float*,
                            const float*,
                            const float*,
                            int,
                            unsigned*,
                            unsigned,
                            int,
                            bool);
using LegacyTrace =
    void (*)(void*, void*, void*, const float*, const float*, const float*, int, int);
std::atomic<Bullet> g_bullet{nullptr};
std::atomic<SlideTrace> g_slide{nullptr};
std::atomic<LegacyTrace> g_legacy{nullptr};
using PhysicsTrace = void (*)(int,
                              void*,
                              const float*,
                              const float*,
                              const float*,
                              const int*,
                              int,
                              int,
                              int,
                              int,
                              const unsigned char*,
                              int);
std::atomic<PhysicsTrace> g_physicsBullet{nullptr}, g_physicsLegacy{nullptr};
using ClientPhysicsTrace = void (*)(int,
                                    void*,
                                    const float*,
                                    const float*,
                                    const float*,
                                    const int*,
                                    int,
                                    int,
                                    int,
                                    int,
                                    const unsigned char*,
                                    int,
                                    bool);
std::atomic<ClientPhysicsTrace> g_physicsClient{nullptr};
std::atomic<unsigned> g_shotSamples{0};
std::shared_ptr<State> Active() {
    return customphysics::OwnsCustomWorld() ? g_state.load() : nullptr;
}
void Break(const std::shared_ptr<State>& state,
           unsigned i,
           const char* cause,
           const float* start,
           const float* end,
           float fraction,
           const glassfile::Vec& normal) {
    if (state->broken[i].exchange(1))
        return;
    Event event{{}, normal, i};
    for (unsigned k = 0; k < 3; ++k)
        event.origin[k] = start[k] + fraction * (end[k] - start[k]);
    // Mantle overlap originates at the player, so project onto the actual pane.
    const auto& p = state->panes[i];
    float d = glassfile::Dot(glassfile::Sub(event.origin, p.vertices[0]), p.normal);
    for (unsigned k = 0; k < 3; ++k)
        event.origin[k] -= d * p.normal[k];
    {
        std::lock_guard lock(state->mutex);
        if (state->events.size() < 1024)
            state->events.push_back(event);
    }
    LOG_INFO("Glass", "pane=%u broken cause=%s surfaces=%zu impact=(%.1f %.1f %.1f)", i, cause,
             p.surfaces.size(), event.origin[0], event.origin[1], event.origin[2]);
}
void Shot(const float* start, const float* end, const void* trace, const char* cause) {
    const auto state = Active();
    if (!state)
        return;
    float limit;
    memcpy(&limit, trace, 4);
    if (!std::isfinite(limit) || limit < 0 || limit > 1)
        return;
    const float point[6]{};
    unsigned hits = 0;
    for (unsigned i = 0; i < state->panes.size(); ++i)
        if (!state->broken[i]) {
            float fraction;
            glassfile::Vec n;
            if (glassfile::Hit(state->panes[i], start, end, point, limit, fraction, n)) {
                Break(state, i, cause, start, end, fraction, n);
                ++hits;
            }
        }
    if (g_shotSamples.load(std::memory_order_relaxed) < 12 &&
        g_shotSamples.fetch_add(1, std::memory_order_relaxed) < 12)
        LOG_INFO("Glass",
                 "%s trace start=(%.1f %.1f %.1f) end=(%.1f %.1f %.1f) fraction=%.6f breaks=%u",
                 cause, start[0], start[1], start[2], end[0], end[1], end[2], limit, hits);
}
void PhysicsBullet(int world,
                   void* trace,
                   const float* start,
                   const float* end,
                   const float* bounds,
                   const int* skip,
                   int count,
                   int children,
                   int mask,
                   int locational,
                   const unsigned char* priority,
                   int phase) {
    g_physicsBullet.load()(world, trace, start, end, bounds, skip, count, children, mask,
                           locational, priority, phase);
    if (locational)
        customcollision::TraceShot(world, trace, start, end, bounds, mask, phase, true);
    if (world >= 0 && world < 5)
        customdoors::Trace(trace, start, end, bounds, mask, locational && mask == 0x2806191);
    // The exact Replay binary has only weapon-bullet and melee callers here.
    if ((world == 0 || world == 1) && locational)
        Shot(start, end, trace, mask == 0x2806191 ? "melee" : "bullet");
}
void PhysicsLegacy(int world,
                   void* trace,
                   const float* start,
                   const float* end,
                   const float* bounds,
                   const int* skip,
                   int count,
                   int children,
                   int mask,
                   int locational,
                   const unsigned char* priority,
                   int phase) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
    g_physicsLegacy.load()(world, trace, start, end, bounds, skip, count, children, mask,
                           locational, priority, phase);
    if (locational)
        customcollision::TraceShot(world, trace, start, end, bounds, mask, phase, false);
    if (world >= 0 && world < 5) {
        customsurfaces::Apply(trace, start, end);
        customdoors::Trace(trace, start, end, bounds, mask, locational && mask == 0x2806191);
    }
    if ((world == 0 || world == 1) && locational &&
        (caller == 0x11D545B || (caller >= 0xFC0550 && caller < 0xFC0800 && mask == 0x2806191)))
        Shot(start, end, trace, mask == 0x2806191 ? "melee" : "bullet");
}
void PhysicsClient(int world,
                   void* trace,
                   const float* start,
                   const float* end,
                   const float* bounds,
                   const int* skip,
                   int count,
                   int children,
                   int mask,
                   int locational,
                   const unsigned char* priority,
                   int phase,
                   bool detectInside) {
    g_physicsClient.load()(world, trace, start, end, bounds, skip, count, children, mask,
                           locational, priority, phase, detectInside);
    // Multiplayer simulates visible bullet hits through this separate client
    // query. Its native caller computes impact position/material after return.
    if (world != 4 || !locational || phase != 0 || mask != 0x2806931)
        return;
    customcollision::TraceShot(world, trace, start, end, bounds, mask, phase, detectInside);
    customdoors::Trace(trace, start, end, bounds, mask);
    Shot(start, end, trace, "client-bullet");
}
bool TraceBullet(void* bp,
                 const void* weapon,
                 bool alternate,
                 void* attacker,
                 void* br,
                 int previous,
                 bool self) {
    float start[3], end[3];
    memcpy(start, static_cast<char*>(bp) + 0x68, 12);
    memcpy(end, static_cast<char*>(bp) + 0x74, 12);
    const auto result = g_bullet.load()(bp, weapon, alternate, attacker, br, previous, self);
    Shot(start, end, br, "bullet-wrapper");
    return result;
}
void TraceSlide(void* self,
                void* pm,
                void* result,
                const float* start,
                const float* end,
                const float* bounds,
                int pass,
                unsigned* ignore,
                unsigned count,
                int mask,
                bool cheap) {
    g_slide.load()(self, pm, result, start, end, bounds, pass, ignore, count, mask, cheap);
    customdoors::Trace(result, start, end, bounds, mask);
    const auto state = Active();
    if (!state || !(mask & 1))
        return;
    float limit;
    memcpy(&limit, result, 4);
    if (!std::isfinite(limit) || limit < 0 || limit > 1)
        return;
    unsigned best = ~0u;
    glassfile::Vec normal{};
    for (unsigned i = 0; i < state->panes.size(); ++i)
        if (!state->broken[i]) {
            float f;
            glassfile::Vec n;
            if (glassfile::Hit(state->panes[i], start, end, bounds, limit, f, n)) {
                best = i;
                limit = f;
                normal = n;
            }
        }
    if (best == ~0u)
        return;
    memset(result, 0, replaytrace::Size);
    auto* bytes = static_cast<unsigned char*>(result);
    replaytrace::WriteContact(bytes, limit, start, end, normal.data());
    const unsigned contents = 1, type = 1;
    const unsigned short entity = 2046;
    memcpy(bytes + 0x20, &contents, 4);
    memcpy(bytes + 0x24, &type, 4);
    memcpy(bytes + 0x2C, &entity, 2);
}
void TraceLegacy(void* self,
                 void* pm,
                 void* result,
                 const float* start,
                 const float* end,
                 const float* bounds,
                 int pass,
                 int mask) {
    g_legacy.load()(self, pm, result, start, end, bounds, pass, mask);
    customsurfaces::Apply(result, start, end);
    customdoors::Trace(result, start, end, bounds, mask);
    const auto state = Active();
    if (!state || mask != 16)
        return;
    // The exact Replay Mantle_Move glass-only overlap query returns here.
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base != 0x11076AE)
        return;

    // Mantle_Move performs a stationary overlap with the player's full
    // capsule. Using those extents as a damage volume reaches nearby vehicle
    // windows while the player is landing on the solid roof. Probe a narrow
    // core around the capsule center instead: it is wide enough to cover
    // movement between frames, but a pane breaks only when the player's body
    // crosses it. Intersecting pane fragments produce one stock break.
    if (!bounds)
        return;
    float centerBounds[6]{};
    glassfile::Vec center{};
    for (unsigned k = 0; k < 3; ++k) {
        centerBounds[k] = bounds[k];
        centerBounds[k + 3] = 4;
        center[k] = start[k] + bounds[k];
    }
    unsigned best = ~0u;
    float bestDistance = INFINITY;
    glassfile::Vec bestNormal{};
    for (unsigned i = 0; i < state->panes.size(); ++i) {
        if (state->broken[i])
            continue;
        float fraction;
        glassfile::Vec normal;
        if (!glassfile::Hit(state->panes[i], start, end, centerBounds, 1, fraction, normal, true))
            continue;
        const float distance = std::abs(glassfile::Dot(
            glassfile::Sub(center, state->panes[i].vertices[0]), state->panes[i].normal));
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
            bestNormal = normal;
        }
    }
    if (best != ~0u)
        Break(state, best, "mantle", center.data(), center.data(), 0, bestNormal);
}
}
namespace customglass {
void PumpEffects() {
    const auto state = Active();
    if (!state)
        return;
    // Called on the existing client/main-thread overlay seam. Physics/server
    // callbacks only enqueue events; particle asset access stays on this thread.
    {
        std::lock_guard lock(state->mutex);
        if (state->events.empty() && state->observations.empty())
            return;
    }
    int connected = 0, deltaTime = 1, time = 0;
    uintptr_t cg = 0;
    // Preserve the native Glass_PlayEffect gates without consuming the queue
    // while the client is transitioning or its predicted time is unavailable.
    if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xEEF1288), &connected, 4) ||
        connected != 9 ||
        !safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xF26F940), &cg, 8) || !cg ||
        !safemem::ReadBytes(reinterpret_cast<void*>(cg + 0x2F20), &deltaTime, 4) || deltaTime ||
        !safemem::ReadBytes(reinterpret_cast<void*>(cg + 0x65A4), &time, 4))
        return;
    const auto now = GetTickCount64();
    for (auto it = state->observations.begin(); it != state->observations.end();) {
        if (now < it->due) {
            ++it;
            continue;
        }
        replayparticle::Snapshot snapshot;
        const bool readable = replayparticle::Inspect(g_base, it->handle, it->definition, snapshot);
        LOG_INFO("Glass",
                 "effect-state pane=%u handle=%08X sample=%u readable=%d present=%d "
                 "running=%d flags=%llX emitters=%u",
                 it->pane, it->handle, it->stage, int(readable), int(snapshot.present),
                 int(snapshot.running), snapshot.flags, snapshot.emitterCount);
        if (readable && snapshot.present)
            for (unsigned i = 0; i < snapshot.emitterCount; ++i)
                LOG_INFO("Glass", "effect-emitter handle=%08X index=%u flags=%X particles=%u",
                         it->handle, i, snapshot.emitters[i].flags, snapshot.emitters[i].particles);
        if (it->stage++ == 0) {
            it->due = now + 500;
            ++it;
        } else {
            it = state->observations.erase(it);
        }
    }
    std::vector<Event> events;
    {
        std::lock_guard lock(state->mutex);
        const auto count = (std::min)(size_t(2), state->events.size());
        events.assign(state->events.begin(), state->events.begin() + count);
        state->events.erase(state->events.begin(), state->events.begin() + count);
    }
    for (const auto& event : events) {
        // The stock effect emits a chip per fracture, not debris for an entire pane.
        const char* name = glassfile::ShatterEffect(state->panes[event.pane]);
        auto* effect = reinterpret_cast<void* (*)(int, const char*, int)>(
            g_base + replay::FindAsset.rva)(44, name, 0);
        uintptr_t assetName = 0;
        char text[160]{};
        const bool found =
            effect && safemem::ReadBytes(effect, &assetName, 8) && assetName &&
            safemem::ReadString(reinterpret_cast<const char*>(assetName), text, sizeof(text)) &&
            strcmp(text, name) == 0;
        unsigned created = 0, requested = 0, firstHandle = 0;
        if (found) {
            float axis[9];
            memcpy(axis, event.normal.data(), 12);
            reinterpret_cast<void (*)(const float*, float*, float*)>(
                g_base + replay::NormalBasis.rva)(axis, axis + 3, axis + 6);
            const auto origins = glassfile::ShardOrigins(state->panes[event.pane], event.normal);
            requested = unsigned(origins.size());
            for (const auto& origin : origins) {
                const unsigned handle =
                    reinterpret_cast<unsigned (*)(int, void*, int, const float*, const float*)>(
                        g_base + replay::PlayOrientedEffect.rva)(0, &effect, time, origin.data(),
                                                                 axis);
                created += handle != 0;
                if (!firstHandle)
                    firstHandle = handle;
            }
        }
        if (firstHandle && state->observationCount < 8) {
            state->observations.push_back(
                {firstHandle, event.pane, 0, reinterpret_cast<uintptr_t>(effect), now + 100});
            ++state->observationCount;
        }
        if (!found || !created || created != requested)
            LOG_WARN("Glass",
                     "effect creation incomplete pane=%u effect='%s' asset=%d systems=%u/%u",
                     event.pane, name, int(found), created, requested);
        auto* alias = reinterpret_cast<void* (*)(const char*)>(
            g_base + replay::SoundAliasByName.rva)("glass_pane_breakout");
        unsigned aliasId = 0;
        if (alias)
            safemem::ReadBytes(static_cast<char*>(alias) + 8, &aliasId, 4);
        if (aliasId)
            reinterpret_cast<void (*)(unsigned, int, int, const float*)>(
                g_base + replay::SoundAtPosition.rva)(aliasId, 0, 2046, event.origin.data());
        LOG_INFO("Glass", "pane=%u shatter='%s' asset=%d systems=%u/%u time=%d positional sound=%d",
                 event.pane, name, int(found), created, requested, time, int(aliasId != 0));
    }
}
void Load(const std::filesystem::path& directory) {
    auto state = std::make_shared<State>();
    const auto path = directory / "glass.bin";
    if (std::filesystem::exists(path) && !glassfile::Load(path, state->panes, state->surfaces)) {
        LOG_ERR("Glass", "invalid glass.bin; refusing pane data");
        g_state.store(nullptr);
        return;
    }
    LOG_INFO("Glass", "loaded %zu independently breakable panes", state->panes.size());
    g_shotSamples = 0;
    g_state.store(std::move(state));
}
void Clear() {
    g_state.store(nullptr);
}
void HideBroken(uintptr_t world, unsigned view) {
    const auto state = g_state.load();
    if (!state || view >= 33)
        return;
    unsigned count = 0;
    uintptr_t visibility = 0;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(world + 0xC8), &count, 4) ||
        count != state->surfaces ||
        !safemem::ReadBytes(reinterpret_cast<void*>(world + 0x40B8 + 8 * view), &visibility, 8) ||
        !visibility)
        return;
    auto* words = reinterpret_cast<unsigned*>(visibility);
    for (unsigned i = 0; i < state->panes.size(); ++i)
        if (state->broken[i].load())
            for (unsigned surface : state->panes[i].surfaces)
                words[surface >> 5] &= ~(0x80000000u >> (surface & 31));
}
hook::Status Install(uintptr_t base) {
    g_base = base;
    for (const auto* b : {&replay::PlayOrientedEffect, &replay::NormalBasis,
                          &replay::SoundAliasByName, &replay::SoundAtPosition}) {
        unsigned char bytes[64]{};
        if (!safemem::ReadBytes(reinterpret_cast<void*>(base + b->rva), bytes, b->size) ||
            memcmp(bytes, b->bytes, b->size))
            return hook::Status::NotReady;
    }
    auto status =
        hook::Install(reinterpret_cast<void*>(base + replay::BulletTrace.rva), &TraceBullet,
                      replay::BulletTrace.bytes, replay::BulletTrace.size, g_bullet);
    if (status != hook::Status::Installed)
        return status;
    status =
        hook::Install(reinterpret_cast<void*>(base + replay::LegacySlideTrace.rva), &TraceSlide,
                      replay::LegacySlideTrace.bytes, replay::LegacySlideTrace.size, g_slide);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::LegacyTrace.rva), &TraceLegacy,
                           replay::LegacyTrace.bytes, replay::LegacyTrace.size, g_legacy);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::PhysicsBulletTrace.rva),
                           &PhysicsBullet, replay::PhysicsBulletTrace.bytes,
                           replay::PhysicsBulletTrace.size, g_physicsBullet);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::PhysicsClientBulletTrace.rva),
                           &PhysicsClient, replay::PhysicsClientBulletTrace.bytes,
                           replay::PhysicsClientBulletTrace.size, g_physicsClient);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::PhysicsLegacyTrace.rva),
                           &PhysicsLegacy, replay::PhysicsLegacyTrace.bytes,
                           replay::PhysicsLegacyTrace.size, g_physicsLegacy);
    if (status == hook::Status::Installed)
        customdoors::Initialize(base, g_physicsLegacy.load());
    return status;
}
}
