#include "custom_doors.h"
#include "door_file.h"
#include "custom_physics.h"
#include "replay_bindings.h"
#include "replay_trace.h"
#include "safemem.h"
#include "logger.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace {
using namespace doorfile;
struct Motion {
    float position = 0;
    unsigned frame = 0, target = 0;
    float duration = 1;
    int lastAction = -1000;
};
struct Actor {
    uintptr_t id = 0;
    Vec origin{};
    std::array<float, 6> bounds{0, 0, 35, 15, 15, 35};
    int time = -1, command = -1;
    bool use = false;
};
struct Sound {
    Vec origin;
    unsigned kind;
};
struct State {
    std::mutex mutex;
    std::vector<Door> doors;
    std::vector<Motion> motion;
    std::array<Actor, 64> actors{};
    std::vector<Sound> sounds;
    unsigned surfaces = 0;
    int time = -1;
    std::string hint;
    ULONGLONG hintAt = 0;
};
std::atomic<std::shared_ptr<State>> current;
uintptr_t base = 0;
customdoors::NativeTrace nativeTrace = nullptr;
std::shared_ptr<State> Active() {
    return customphysics::OwnsCustomWorld() ? current.load() : nullptr;
}
Vec Center(const Door& d, unsigned frame) {
    Vec v{};
    for (unsigned k = 0; k < 3; ++k)
        v[k] = (d.mins[k] + d.maxs[k]) * .5f;
    return Point(d, v, frame);
}
void Operate(State& s, unsigned index, const Vec& actor, bool bash) {
    auto& m = s.motion[index];
    const auto& d = s.doors[index];
    if (s.time - m.lastAction < 250)
        return;
    const bool close = !bash && m.target != d.Closed();
    if (bash && m.target != d.Closed())
        return;
    int sign = 1;
    if (d.angle) {
        const auto center = Center(d, d.Closed());
        const Vec tangent{-(center[1] - d.pivot[1]) * d.angle, (center[0] - d.pivot[0]) * d.angle,
                          0};
        const Vec delta{actor[0] - center[0], actor[1] - center[1], actor[2] - center[2]};
        if (Dot(delta, tangent) > 0)
            sign = -1;
    }
    for (unsigned i = 0; i < s.doors.size(); ++i) {
        const auto& leaf = s.doors[i];
        if (leaf.group != d.group)
            continue;
        auto& move = s.motion[i];
        move.target = close ? leaf.Closed()
                            : (leaf.angle && sign < 0 ? 0 : unsigned(leaf.surfaces.size() - 1));
        move.duration = bash ? (leaf.angle ? .22f : .9f) : leaf.duration;
        move.lastAction = s.time;
    }
    if (s.sounds.size() < 64)
        s.sounds.push_back({Center(d, m.frame), bash ? 2u : close ? 1u : 0u});
    LOG_INFO("Doors", "%s: %s group=%u", d.name,
             bash    ? "bash open"
             : close ? "close"
                     : "open",
             d.group);
}
bool Occupied(const State& s, const Door& d, unsigned frame) {
    for (const auto& actor : s.actors) {
        if (!actor.id || s.time - actor.time > 250)
            continue;
        float fraction;
        Vec normal;
        if (Hit(d, frame, actor.origin.data(), actor.origin.data(), actor.bounds.data(), 1,
                fraction, normal, true))
            return true;
    }
    return false;
}
void Advance(State& s, int time) {
    if (time <= s.time)
        return;
    const int elapsed = s.time < 0 ? 0 : (std::min)(time - s.time, 100);
    s.time = time;
    for (unsigned i = 0; i < s.doors.size(); ++i) {
        auto& m = s.motion[i];
        const auto& d = s.doors[i];
        const float step = elapsed * 24.f / (m.duration * 1000.f);
        float next = m.position + (m.target > m.position ? step : -step);
        next = m.target > m.position ? (std::min)(next, float(m.target))
                                     : (std::max)(next, float(m.target));
        const unsigned frame = unsigned(std::lround(next));
        // Check every intervening pose, including fast bash motion.
        bool blocked = false;
        int pose = int(m.frame), direction = frame > m.frame ? 1 : -1;
        while (pose != int(frame)) {
            pose += direction;
            if (Occupied(s, d, unsigned(pose))) {
                blocked = true;
                break;
            }
        }
        if (blocked) {
            if (m.target == d.Closed()) {
                const auto target = m.position < d.Closed() ? 0u : unsigned(d.surfaces.size() - 1);
                for (unsigned j = 0; j < s.doors.size(); ++j)
                    if (s.doors[j].group == d.group)
                        s.motion[j].target = target;
            }
            continue;
        }
        m.position = next;
        m.frame = frame;
    }
}
bool Visible(const Vec& eye, const Vec& target) {
    if (!nativeTrace)
        return false;
    alignas(16) unsigned char trace[0x48]{};
    const float bounds[6]{};
    const int skip = 0;
    nativeTrace(0, trace, eye.data(), target.data(), bounds, &skip, 1, 1, 0x2806191, 0, nullptr, 0);
    float fraction = 0;
    std::memcpy(&fraction, trace, 4);
    return std::isfinite(fraction) && fraction >= .98f;
}
}
namespace customdoors {
void Initialize(uintptr_t address, NativeTrace trace) {
    base = address;
    nativeTrace = trace;
}
void Load(const std::filesystem::path& directory) {
    auto state = std::make_shared<State>();
    const auto path = directory / "doors.bin";
    if (!std::filesystem::exists(path)) {
        Clear();
        return;
    }
    if (!doorfile::Load(path, state->doors, state->surfaces)) {
        LOG_ERR("Doors", "invalid doors.bin; refusing dynamic door data");
        Clear();
        return;
    }
    for (const auto& d : state->doors) {
        Motion m;
        m.position = float(d.Closed());
        m.frame = m.target = d.Closed();
        m.duration = d.duration;
        state->motion.push_back(m);
        LOG_INFO("Doors", "loaded %s group=%u poses=%zu hulls=%zu", d.name, d.group,
                 d.surfaces.size(), d.hulls.size());
    }
    current.store(std::move(state));
}
void Clear() {
    current.store(nullptr);
}
void Movement(void* pm, void* pml) {
    const auto state = Active();
    if (!state || state->doors.empty())
        return;
    uintptr_t ps = 0;
    int type = -1, time = -1;
    uint64_t buttons = 0;
    Vec origin{}, forward{}, velocity{};
    if (!safemem::ReadBytes(static_cast<char*>(pm) + 8, &ps, 8) || !ps ||
        !safemem::ReadBytes(reinterpret_cast<void*>(ps + 0xC), &type, 4) || type != 0 ||
        !safemem::ReadBytes(reinterpret_cast<void*>(ps + 0x30), origin.data(), 12) ||
        !safemem::ReadBytes(reinterpret_cast<void*>(ps + 0x3C), velocity.data(), 12) ||
        !safemem::ReadBytes(pml, forward.data(), 12) ||
        !safemem::ReadBytes(static_cast<char*>(pm) + 0x10, &buttons, 8) ||
        !safemem::ReadBytes(static_cast<char*>(pm) + 0x1C, &time, 4) || time < 0)
        return;
    for (float x : origin)
        if (!std::isfinite(x) || std::abs(x) > 100000)
            return;
    if (!std::isfinite(Dot(forward, forward)) || std::abs(Dot(forward, forward) - 1) > .05f)
        return;
    std::lock_guard lock(state->mutex);
    auto actor = std::find_if(state->actors.begin(), state->actors.end(), [&](const Actor& a) {
        return a.id == ps;
    });
    if (actor == state->actors.end())
        actor = std::min_element(state->actors.begin(), state->actors.end(),
                                 [](const Actor& a, const Actor& b) {
                                     return a.time < b.time;
                                 });
    if (actor->id != ps)
        *actor = Actor{ps};
    const bool fresh = time > actor->command;
    // Replay uses Activate (keyboard) and Use/Reload (controller) separately.
    const bool use = (buttons & 0x28) != 0;
    const bool pressed = fresh && use && !actor->use;
    actor->origin = origin;
    actor->time = time;
    uintptr_t boundsPointer = 0;
    std::array<float, 6> bounds{};
    if (safemem::ReadBytes(static_cast<char*>(pm) + 0x360, &boundsPointer, 8) && boundsPointer &&
        safemem::ReadBytes(reinterpret_cast<void*>(boundsPointer), bounds.data(), sizeof(bounds)) &&
        std::all_of(bounds.begin(), bounds.end(),
                    [](float v) {
                        return std::isfinite(v) && std::abs(v) < 256;
                    }) &&
        bounds[3] > 0 && bounds[4] > 0 && bounds[5] > 0)
        actor->bounds = bounds;
    int clientNum = -1;
    safemem::ReadBytes(reinterpret_cast<void*>(ps + 0x1C0), &clientNum, 4);
    if (fresh) {
        actor->command = time;
        actor->use = use;
    }
    Advance(*state, time);
    Vec eye = origin;
    eye[2] += actor->bounds[2] + actor->bounds[5] - 8;
    Vec end{};
    for (unsigned k = 0; k < 3; ++k)
        end[k] = eye[k] + forward[k] * 80;
    const float pointBounds[6]{};
    unsigned best = ~0u;
    float limit = 1;
    Vec target{};
    for (unsigned i = 0; i < state->doors.size(); ++i) {
        float fraction;
        Vec normal;
        if (Hit(state->doors[i], state->motion[i].frame, eye.data(), end.data(), pointBounds, limit,
                fraction, normal)) {
            best = i;
            limit = fraction;
            for (unsigned k = 0; k < 3; ++k)
                target[k] = eye[k] + (end[k] - eye[k]) * fraction;
        }
    }
    if (clientNum == 0)
        state->hint.clear();
    if (best == ~0u || !Visible(eye, target))
        return;
    const float distance = limit * limit * 80 * 80;
    const auto& d = state->doors[best];
    const auto& m = state->motion[best];
    if (clientNum == 0) {
        state->hint = m.target == d.Closed() ? "Open Door" : "Close Door";
        state->hintAt = GetTickCount64();
    }
    const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    Vec approach{target[0] - eye[0], target[1] - eye[1], 0};
    const bool bash = fresh && distance < 32 * 32 && speed > 190 && Dot(approach, velocity) > 100;
    if (pressed || bash)
        Operate(*state, best, eye, bash);
}
void Trace(
    void* result, const float* start, const float* end, const float* bounds, int mask, bool melee) {
    const auto state = Active();
    if (!state || !(mask & 1))
        return;
    float limit = 0;
    std::memcpy(&limit, result, 4);
    if (!std::isfinite(limit) || limit < 0 || limit > 1)
        return;
    std::lock_guard lock(state->mutex);
    unsigned best = ~0u;
    Vec normal{};
    for (unsigned i = 0; i < state->doors.size(); ++i) {
        float fraction;
        Vec n;
        if (Hit(state->doors[i], state->motion[i].frame, start, end, bounds, limit, fraction, n)) {
            best = i;
            limit = fraction;
            normal = n;
        }
    }
    if (best == ~0u)
        return;
    if (melee)
        Operate(*state, best, {start[0], start[1], start[2]}, true);
    std::memset(result, 0, 0x48);
    auto bytes = static_cast<char*>(result);
    replaytrace::WriteContact(bytes, limit, start, end, normal.data());
    const unsigned flags = 21u << 19, contents = 1, hitType = 1;
    const unsigned short entity = 2046;
    std::memcpy(bytes + 0x1C, &flags, 4);
    std::memcpy(bytes + 0x20, &contents, 4);
    std::memcpy(bytes + 0x24, &hitType, 4);
    std::memcpy(bytes + 0x2C, &entity, 2);
}
void Visibility(uintptr_t world, unsigned view) {
    const auto state = current.load();
    if (!state || view >= 33)
        return;
    unsigned count = 0;
    uintptr_t visibility = 0;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(world + 0xC8), &count, 4) ||
        count != state->surfaces ||
        !safemem::ReadBytes(reinterpret_cast<void*>(world + 0x40B8 + 8 * view), &visibility, 8) ||
        !visibility)
        return;
    std::lock_guard lock(state->mutex);
    auto words = reinterpret_cast<unsigned*>(visibility);
    for (unsigned i = 0; i < state->doors.size(); ++i)
        for (unsigned frame = 0; frame < state->doors[i].surfaces.size(); ++frame)
            if (frame != state->motion[i].frame)
                for (unsigned id : state->doors[i].surfaces[frame])
                    words[id >> 5] &= ~(0x80000000u >> (id & 31));
}
std::string Hint() {
    const auto state = Active();
    if (!state)
        return {};
    std::lock_guard lock(state->mutex);
    return GetTickCount64() - state->hintAt <= 250 ? state->hint : std::string{};
}
void PumpSounds() {
    const auto state = Active();
    if (!state || !base)
        return;
    std::vector<Sound> sounds;
    {
        std::lock_guard lock(state->mutex);
        sounds.swap(state->sounds);
    }
    for (const auto& event : sounds) {
        const char* names[] = {"scrpt_door_wood_single_open", "scrpt_door_wood_single_close",
                               "scrpt_door_wood_single_bash"};
        auto alias = reinterpret_cast<void* (*)(const char*)>(base + replay::SoundAliasByName.rva)(
            names[event.kind]);
        unsigned id = 0;
        if (alias)
            safemem::ReadBytes(static_cast<char*>(alias) + 8, &id, 4);
        if (id)
            reinterpret_cast<void (*)(unsigned, int, int, const float*)>(
                base + replay::SoundAtPosition.rva)(id, 0, 2046, event.origin.data());
        LOG_INFO("Doors", "sound=%s resolved=%d", names[event.kind], int(id != 0));
    }
}
}
