#include "custom_doors.h"
#include "custom_render.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include "custom_glass.h"
#include <atomic>
#include <cstring>

namespace {
uintptr_t g_base = 0;
using Query = void (*)(const void*);
std::atomic<Query> g_query{nullptr};
std::atomic<bool> g_reported{false};
std::atomic<bool> g_invalidReported{false};
bool SelectedWorld(const char* name) {
    try {
        const auto active = custommaps::Active();
        return !active.empty() && ("maps/mp/" + active + ".d3dbsp") == name;
    } catch (...) {
        return false;
    }
}

template <typename T>
bool ReadWorld(uintptr_t world, size_t offset, T& value) {
    return safemem::ReadBytes(reinterpret_cast<const void*>(world + offset), &value,
                              sizeof(value));
}

bool ValidGeneratedWorld(uintptr_t world, const char*& error) {
    unsigned bspVersion = 0;
    unsigned lastSun = 0;
    unsigned lightCount = 0;
    unsigned firstMutable = 0;
    unsigned mutableCount = 0;
    unsigned firstStaticScriptable = 0;
    unsigned staticScriptableCount = 0;
    unsigned firstScriptable = 0;
    unsigned scriptableCount = 0;
    unsigned firstMovingScriptable = 0;
    unsigned movingScriptableCount = 0;
    unsigned cellCount = 0;
    unsigned surfaceCount = 0;
    unsigned surfaceDataCount = 0;
    unsigned modelCount = 0;
    unsigned visibilityWords = 0;
    unsigned transientCount = 0;
    unsigned primaryLightVisibilityWords = 0;
    unsigned umbraGateCount = 0;
    unsigned tomeSize = 0;
    uintptr_t nodes = 0;
    uintptr_t sceneEntCellBits = 0;
    uintptr_t cells = 0;
    uintptr_t cellTransientInfos = 0;
    uintptr_t surfaces = 0;
    uintptr_t surfaceBounds = 0;
    uintptr_t surfaceData = 0;
    uintptr_t surfaceMaterials = 0;
    uintptr_t transientZones = 0;
    uintptr_t primaryLights = 0;
    uintptr_t models = 0;
    uintptr_t cellVisibility = 0;
    uintptr_t sunLitCells = 0;
    uintptr_t primaryLightVisibility = 0;
    uintptr_t umbraGateNames = 0;
    uintptr_t tomeData = 0;
    uintptr_t tome = 0;

    const bool read =
        ReadWorld(world, 0x10, bspVersion) && ReadWorld(world, 0x14, lastSun) &&
        ReadWorld(world, 0x18, lightCount) && ReadWorld(world, 0x1C, firstMutable) &&
        ReadWorld(world, 0x20, mutableCount) &&
        ReadWorld(world, 0x24, firstStaticScriptable) &&
        ReadWorld(world, 0x28, staticScriptableCount) &&
        ReadWorld(world, 0x2C, firstScriptable) && ReadWorld(world, 0x30, scriptableCount) &&
        ReadWorld(world, 0x34, firstMovingScriptable) &&
        ReadWorld(world, 0x38, movingScriptableCount) && ReadWorld(world, 0x90, cellCount) &&
        ReadWorld(world, 0xA8, nodes) && ReadWorld(world, 0xB0, sceneEntCellBits) &&
        ReadWorld(world, 0xB8, cells) && ReadWorld(world, 0xC0, cellTransientInfos) &&
        ReadWorld(world, 0xC8, surfaceCount) && ReadWorld(world, 0xCC, surfaceDataCount) &&
        ReadWorld(world, 0xF0, surfaces) && ReadWorld(world, 0xF8, surfaceBounds) &&
        ReadWorld(world, 0x100, surfaceData) && ReadWorld(world, 0x108, surfaceMaterials) &&
        ReadWorld(world, 0x7CC, transientCount) && ReadWorld(world, 0x7D0, transientZones) &&
        ReadWorld(world, 0x3D68, primaryLights) && ReadWorld(world, 0x3DF0, modelCount) &&
        ReadWorld(world, 0x3DF8, models) && ReadWorld(world, 0x3EF0, cellVisibility) &&
        ReadWorld(world, 0x3EF8, sunLitCells) && ReadWorld(world, 0x3F9C, visibilityWords) &&
        ReadWorld(world, 0x3FA0, primaryLightVisibilityWords) &&
        ReadWorld(world, 0x41C0, primaryLightVisibility) &&
        ReadWorld(world, 0x4440, umbraGateCount) &&
        ReadWorld(world, 0x4448, umbraGateNames) && ReadWorld(world, 0x4450, tomeSize) &&
        ReadWorld(world, 0x4458, tomeData) && ReadWorld(world, 0x4460, tome);
    if (!read) {
        error = "unreadable GfxWorld fields";
        return false;
    }
    if (bspVersion != 243 || lightCount != 2 || lastSun != 1) {
        error = "wrong BSP or primary-light header";
        return false;
    }
    if (firstMutable != lightCount || firstStaticScriptable != lightCount ||
        firstScriptable != lightCount || firstMovingScriptable != lightCount || mutableCount ||
        staticScriptableCount || scriptableCount || movingScriptableCount) {
        error = "inconsistent primary-light ranges";
        return false;
    }
    if (cellCount != 1 || !nodes || !sceneEntCellBits || !cells || !cellTransientInfos ||
        transientCount != 1 || !transientZones) {
        error = "incomplete one-cell visibility world";
        return false;
    }
    if (surfaceCount > 4096 || surfaceDataCount != surfaceCount ||
        visibilityWords != (surfaceCount + 31) / 32 ||
        (surfaceCount && (!surfaces || !surfaceBounds || !surfaceData || !surfaceMaterials))) {
        error = "inconsistent surface arrays";
        return false;
    }
    if (!primaryLights || modelCount != 1 || !models || !cellVisibility || !sunLitCells ||
        primaryLightVisibilityWords != 1 || !primaryLightVisibility) {
        error = "incomplete light or model visibility storage";
        return false;
    }
    if (umbraGateCount || umbraGateNames || tomeSize || tomeData || tome) {
        error = "mixed Umbra and all-visible data";
        return false;
    }
    return true;
}
void QueryVisibility(const void* command) {
    const DWORD saved = GetLastError();
    unsigned job = ~0u, view = ~0u;
    uintptr_t world = 0, tome = ~uintptr_t{}, name = 0;
    char text[64]{};
    // Replay's command has jobIndex at 7C and sceneViewType at 8C. Execute
    // once, before the native worker decrements its job-completion counter.
    // The stock null-tome branch otherwise clears visibility and never fills it.
    if (safemem::ReadBytes(static_cast<const char*>(command) + 0x7C, &job, 4) && job == 0 &&
        safemem::ReadBytes(static_cast<const char*>(command) + 0x8C, &view, 4) && view < 33 &&
        safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x10C77870), &world, 8) && world &&
        safemem::ReadBytes(reinterpret_cast<void*>(world + 0x4460), &tome, 8) && !tome &&
        safemem::ReadBytes(reinterpret_cast<void*>(world), &name, 8) &&
        safemem::ReadString(reinterpret_cast<const char*>(name), text, sizeof(text)) &&
        SelectedWorld(text) && custommaps::ActiveWorldContract()) {
        const char* contractError = nullptr;
        if (!ValidGeneratedWorld(world, contractError)) {
            if (!g_invalidReported.exchange(true))
                LOG_ERR("Render", "rejected malformed generated GfxWorld: %s", contractError);
            SetLastError(saved);
            g_query.load()(command);
            return;
        }
        // The engine already uses this conservative fallback when an Umbra
        // query fails. It fills the native surface/light visibility masks and
        // masks unused tail bits; downstream draw and frustum processing stay native.
        reinterpret_cast<void (*)(unsigned)>(g_base + replay::UmbraSetAllVisible.rva)(view);
        customglass::HideBroken(world, view);
        customdoors::Visibility(world, view);
        if (!g_reported.exchange(true))
            LOG_INFO(
                "Render",
                "validated all-visible-v1 GfxWorld; enabled native conservative visibility fallback view=%u",
                view);
    }
    SetLastError(saved);
    g_query.load()(command);
}
}
namespace customrender {
hook::Status Install(uintptr_t base) {
    if (g_query.load())
        return hook::Status::Installed;
    g_base = base;
    const auto& fallback = replay::UmbraSetAllVisible;
    unsigned char bytes[64]{};
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + fallback.rva), bytes, fallback.size) ||
        std::memcmp(bytes, fallback.bytes, fallback.size) != 0)
        return hook::Status::NotReady;
    const auto& query = replay::UmbraQueryStaticVisibilityCmd;
    return hook::Install(reinterpret_cast<void*>(base + query.rva), &QueryVisibility,
                         query.bytes, query.size, g_query);
}
}
