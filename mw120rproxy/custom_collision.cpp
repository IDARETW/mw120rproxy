#include "custom_doors.h"
#include "custom_collision.h"
#include "custom_physics.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "test_brushes.h"
#include "collision_file.h"
#include "collision_ray.h"
#include "compound_collision.h"
#include "custom_ladders.h"
#include "custom_glass.h"
#include "custom_surfaces.h"
#include "safemem.h"
#include "logger.h"
#include <atomic>
#include <cstring>
#include <iterator>
#include <mutex>
#include <memory>

namespace {
uintptr_t g_base = 0;
using WorldCall = uintptr_t (*)(int);
std::atomic<WorldCall> g_create{nullptr}, g_shutdown{nullptr};
struct Body {
    void* shape = nullptr;
    unsigned instance = ~0u;
};
// Replay WorldCollision has five physics worlds (native instance array is 20 bytes).
std::vector<Body> g_bodies[5];
bool g_loaded[5]{};
// Havok shapes are immutable and native body creation retains a reference.
// Keep ONE constructor reference per shape for all five worlds, otherwise large
// imported maps exhaust the fixed Havok allocator during the fourth load.
std::vector<void*> g_shapes;
std::string g_shapeMap;
std::recursive_mutex g_shapeMutex;
std::atomic<std::shared_ptr<const collisionray::World>> g_rays;
std::atomic<unsigned> g_raySamples[5]{};
template <class T> T Function(const replay::Binding& b) {
    return reinterpret_cast<T>(g_base + b.rva);
}
bool Matches(const replay::Binding& b) {
    unsigned char p[64]{};
    return safemem::ReadBytes(reinterpret_cast<void*>(g_base + b.rva), p, b.size) &&
           memcmp(p, b.bytes, b.size) == 0;
}

void* MakeShape(const collisionfile::Brush& brush) {
    alignas(16) float mins[4]{}, maxs[4]{};
    for (unsigned j = 0; j < 3; ++j) {
        mins[j] = brush.mins[j] / 32.0f;
        maxs[j] = brush.maxs[j] / 32.0f;
    }
    bool axial = brush.vertices.size() == 8;
    for (const auto& v : brush.vertices)
        for (unsigned k = 0; k < 3; ++k)
            if (std::abs(v[k] - brush.mins[k]) > 0.001f && std::abs(v[k] - brush.maxs[k]) > 0.001f)
                axial = false;
    if (brush.vertices.empty() || axial)
        return Function<void* (*)(const float*, const float*)>(replay::CreateShapeAabb)(mins, maxs);
    alignas(16) float points[252][4]{};
    for (size_t p = 0; p < brush.vertices.size(); ++p)
        for (unsigned k = 0; k < 3; ++k)
            points[p][k] = brush.vertices[p][k] / 32.0f;
    struct Array {
        void* data;
        int size;
        uint32_t capacity;
    } array{points, static_cast<int>(brush.vertices.size()), 0x80000000u | 252u};
    return Function<void* (*)(Array*, unsigned, bool)>(replay::CreateShapeConvex)(&array, 252,
                                                                                  false);
}

void Destroy(int world) {
    std::lock_guard lock(g_shapeMutex);
    if (world < 0 || world >= 5 || !g_loaded[world])
        return;
    unsigned destroyed = 0;
    for (auto& body : g_bodies[world]) {
        if (body.instance != ~0u) {
            Function<void (*)(int, unsigned, bool)>(replay::DestroyPhysicsInstance)(
                world, body.instance, false);
            body.instance = ~0u;
            ++destroyed;
        }
        body.shape = nullptr; // Borrowed from the shared constructor-reference cache.
    }
    g_loaded[world] = false;
    g_bodies[world].clear();
    if (std::none_of(std::begin(g_loaded), std::end(g_loaded), [](bool v) {
            return v;
        })) {
        for (auto* shape : g_shapes)
            if (shape)
                Function<void (*)(void*)>(replay::RemoveHavokReference)(shape);
        g_shapes.clear();
        g_shapeMap.clear();
        g_rays.store(nullptr);
    }
    LOG_INFO("Collision", "custom brush bodies destroyed world=%d count=%u", world, destroyed);
}
uintptr_t Shutdown(int world) {
    const DWORD saved = GetLastError();
    Destroy(world);
    if (world == 0) {
        customglass::Clear();
        customdoors::Clear();
        customsurfaces::Clear();
    }
    SetLastError(saved);
    return g_shutdown.load()(world);
}
uintptr_t Create(int world) {
    const auto result = g_create.load()(world);
    const DWORD saved = GetLastError();
    std::lock_guard lock(g_shapeMutex);
    const auto active = custommaps::Active();
    if (world >= 0 && world < 5 && !g_loaded[world] && customphysics::OwnsEmptyWorld() &&
        !active.empty()) {
        uintptr_t stockShapes = ~uintptr_t{};
        safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xE5C62F8), &stockShapes,
                           sizeof(stockShapes));
        if (!stockShapes) {
            std::vector<collisionfile::Brush> brushes;
            std::wstring zonePath;
            if (!custommaps::ActiveZonePath(active.c_str(), zonePath)) {
                SetLastError(saved);
                return result;
            }
            const auto path = std::filesystem::path(zonePath).parent_path() / "collision.bin";
            customladders::Load(path.parent_path());
            if (world == 0) {
                customglass::Load(path.parent_path());
                customdoors::Load(path.parent_path());
                customsurfaces::Load(path.parent_path());
            }
            std::error_code pathError;
            const bool hasCollision = std::filesystem::exists(path, pathError);
            if (pathError) {
                LOG_ERR("Collision", "cannot inspect package collision.bin");
                SetLastError(saved);
                return result;
            }
            if (hasCollision) {
                if (!collisionfile::Load(path, brushes)) {
                    LOG_ERR(
                        "Collision",
                        "invalid package collision.bin; refusing mismatched fallback collision");
                    SetLastError(saved);
                    return result;
                }
            } else if (active == "mp_test") {
                // v12/v13 packages predate data-driven authoring. Preserve their
                // proven arena collision; authored manifests require the file.
                for (const auto& b : testbrushes::brushes) {
                    collisionfile::Brush out{};
                    memcpy(out.mins, b.mins, 12);
                    memcpy(out.maxs, b.maxs, 12);
                    brushes.push_back(out);
                }
            } else {
                LOG_ERR("Collision", "selected package has no collision.bin");
                SetLastError(saved);
                return result;
            }
            const bool compound = brushes.size() > compoundcollision::Threshold;
            const auto batches = compoundcollision::Batches(brushes);
            const bool reuse = !g_shapes.empty();
            const auto bodyCount = batches.size();
            if (reuse && (g_shapeMap != active || g_shapes.size() != bodyCount)) {
                LOG_ERR(
                    "Collision",
                    "shared shape cache belongs to another active map; refusing mismatched collision");
                SetLastError(saved);
                return result;
            }
            if (!reuse) {
                auto rays = std::make_shared<collisionray::World>();
                if (!rays->Build(brushes)) {
                    LOG_ERR("Collision", "cannot build bullet collision hulls for map=%s",
                            active.c_str());
                    SetLastError(saved);
                    return result;
                }
                g_rays.store(std::move(rays));
                for (auto& samples : g_raySamples)
                    samples = 0;
                g_shapes.resize(bodyCount);
                g_shapeMap = active;
            }
            g_bodies[world].resize(bodyCount);
            g_loaded[world] = true;
            LOG_INFO("Collision", "initializing map=%s world=%d hulls=%zu bodies=%zu compound=%d",
                     active.c_str(), world, brushes.size(), g_bodies[world].size(), compound);
            const float origin[3]{}, identity[4]{0, 0, 0, 1};
            unsigned count = 0;
            for (size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
                const auto& batch = batches[batchIndex];
                const size_t i = batch.brushes.front();
                const auto& brush = brushes[i];
                // Native hknp constructors use meters; engine world units are inches/32.
                auto& body = g_bodies[world][batchIndex];
                if (reuse)
                    body.shape = g_shapes[batchIndex];
                else if (!compound)
                    body.shape = MakeShape(brush);
                else {
                    std::vector<compoundcollision::Instance> instances(batch.brushes.size());
                    bool complete = true;
                    for (size_t j = 0; j < batch.brushes.size(); ++j) {
                        instances[j].shape = MakeShape(brushes[batch.brushes[j]]);
                        if (!instances[j].shape) {
                            complete = false;
                            break;
                        }
                    }
                    if (complete) {
                        compoundcollision::Array array{
                            instances.data(), static_cast<int>(instances.size()),
                            0x80000000u | static_cast<uint32_t>(instances.size())};
                        body.shape = Function<void* (*)(compoundcollision::Array*)>(
                            replay::CreateShapeCompound)(&array);
                    }
                    // Native compound construction copies instances and retains
                    // each child. Drop the original constructor references.
                    for (auto& instance : instances)
                        if (instance.shape)
                            Function<void (*)(void*)>(replay::RemoveHavokReference)(instance.shape);
                }
                if (!body.shape) {
                    LOG_ERR("Collision", "native shape constructor failed world=%d brush=%zu",
                            world, i);
                    break;
                }
                if (!reuse)
                    g_shapes[batchIndex] = body.shape;
                using Instantiate = unsigned (*)(int, const void*, int, const char*, const char*,
                                                 int, const float*, const float*, bool, bool, bool);
                // A compound's filter belongs to the body, so every child must
                // share its contents. Player clip must not stop bullets or light.
                body.instance = Function<Instantiate>(replay::InstantiateStaticBody)(
                    world, body.shape, 0, "custom-map-brush", "PM_Concrete", int(batch.contents),
                    origin, identity, true, true, false);
                if (body.instance == ~0u) {
                    LOG_ERR("Collision", "native body creation failed world=%d brush=%zu", world,
                            i);
                    break;
                }
                ++count;
                if (count <= 4 || count % 32 == 0 || count == g_bodies[world].size())
                    LOG_INFO(
                        "Collision",
                        "world=%d brush=%zu/%zu instance=%u shape=%p bounds=(%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
                        world, i + 1, brushes.size(), body.instance, body.shape, brush.mins[0],
                        brush.mins[1], brush.mins[2], brush.maxs[0], brush.maxs[1], brush.maxs[2]);
            }
            if (count != g_bodies[world].size())
                Destroy(world);
            else {
                LOG_INFO(
                    "Collision",
                    "native collision initialized world=%d hulls=%zu bodies=%u shared_shapes=%d",
                    world, brushes.size(), count, int(reuse));
            }
        }
    }
    SetLastError(saved);
    return result;
}
}
namespace customcollision {
bool Visible(const float* start, const float* end) {
    if (!start || !end)
        return false;
    const auto rays = g_rays.load();
    if (!rays)
        return false;
    collisionray::Vec a{}, b{};
    for (unsigned k = 0; k < 3; ++k) {
        if (!std::isfinite(start[k]) || !std::isfinite(end[k]))
            return false;
        a[k] = start[k];
        b[k] = end[k];
    }
    collisionray::Hit hit{1.0};
    return !rays->Trace(a, b, hit, true, collisionfile::Solid);
}
void TraceShot(int world,
               void* trace,
               const float* start,
               const float* end,
               const float* bounds,
               int mask,
               int phase,
               bool detectInside) {
    // Bullet and melee callers use All (0). Other phases, swept bounds and
    // unsupported contents retain the original native query result.
    if (world < 0 || world >= 5 || phase != 0 || !(mask & collisionfile::SupportedContents) ||
        !trace || !start || !end || !bounds)
        return;
    for (unsigned k = 0; k < 6; ++k)
        if (bounds[k] != 0)
            return;
    collisionray::Vec a{}, b{};
    for (unsigned k = 0; k < 3; ++k) {
        if (!std::isfinite(start[k]) || !std::isfinite(end[k]))
            return;
        a[k] = start[k];
        b[k] = end[k];
    }
    if (a == b)
        return;
    float original;
    memcpy(&original, trace, 4);
    if (!std::isfinite(original) || original < 0 || original > 1)
        return;
    auto* bytes = static_cast<unsigned char*>(trace);
    unsigned originalType;
    unsigned short originalId;
    memcpy(&originalType, bytes + 0x24, 4);
    memcpy(&originalId, bytes + 0x2C, 2);
    const auto rays = g_rays.load();
    const bool retagged = rays && originalType == 0 && original > 0 && original < 1 && !bytes[0x3D];
    bool rewritten = false;
    collisionray::Hit hit{originalType ? original : 1.0};
    if (!retagged && rays && rays->Trace(a, b, hit, detectInside, uint32_t(mask)) &&
        !hit.startSolid) {
        rewritten = true;
        memset(bytes, 0, 0x48);
        const float fraction = float(hit.fraction);
        float normal[3];
        for (unsigned k = 0; k < 3; ++k) {
            normal[k] = float(hit.normal[k]);
        }
        replaytrace::WriteContact(bytes, fraction, start, end, normal);
        const unsigned contents = hit.contents, hitType = 1, worldEntity = 2046;
        memcpy(bytes + 0x20, &contents, 4);
        memcpy(bytes + 0x24, &hitType, 4);
        memcpy(bytes + 0x2C, &worldEntity, 4);
        // allsolid/startsolid offsets are verified against Replay's conversion.
        bytes[0x3C] = hit.allSolid;
        bytes[0x3D] = hit.startSolid;
        bytes[0x3E] = hit.normal[2] >= .7;
    } else if (retagged) {
        // Custom Havok bodies deliberately have no stock asset/entity reference.
        // Their native trace geometry is valid, but Replay encodes the result as
        // hit type 0 and its weapon consumer discards the impact. Preserve every
        // native contact field and attach the normal world-entity identity.
        const unsigned hitType = 1, worldEntity = 2046;
        memcpy(bytes + 0x24, &hitType, 4);
        memcpy(bytes + 0x2C, &worldEntity, 4);
    }
    customsurfaces::ApplyShot(trace, start, end);
    if (g_raySamples[world].fetch_add(1) < 12) {
        float fraction;
        unsigned flags, finalType;
        unsigned short finalId;
        memcpy(&fraction, trace, 4);
        memcpy(&flags, static_cast<unsigned char*>(trace) + 0x1C, 4);
        memcpy(&finalType, bytes + 0x24, 4);
        memcpy(&finalId, bytes + 0x2C, 2);
        LOG_INFO(
            "BulletCollision",
            "world=%d native=%.6f type=%u id=%u final=%.6f type=%u id=%u rewritten=%d retagged=%d surface=%u",
            world, original, originalType, originalId, fraction, finalType, finalId, int(rewritten),
            int(retagged), (flags >> 19) & 63);
    }
}
hook::Status Install(uintptr_t base) {
    if (g_create.load() && g_shutdown.load())
        return hook::Status::Installed;
    g_base = base;
    for (const auto* b : {&replay::CreateShapeAabb, &replay::CreateShapeConvex,
                          &replay::CreateShapeCompound, &replay::InstantiateStaticBody,
                          &replay::DestroyPhysicsInstance, &replay::RemoveHavokReference})
        if (!Matches(*b))
            return hook::Status::NotReady;
    auto status = hook::Install(reinterpret_cast<void*>(base + replay::WorldCollisionShutdown.rva),
                                &Shutdown, replay::WorldCollisionShutdown.bytes,
                                replay::WorldCollisionShutdown.size, g_shutdown);
    if (status != hook::Status::Installed)
        return status;
    return hook::Install(reinterpret_cast<void*>(base + replay::WorldCollisionCreate.rva), &Create,
                         replay::WorldCollisionCreate.bytes, replay::WorldCollisionCreate.size,
                         g_create);
}
}
