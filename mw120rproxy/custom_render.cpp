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
std::atomic<Query> g_draw{nullptr};
std::atomic<bool> g_reported{false};
std::atomic<unsigned> g_frames{0};
using BspDraw = void (*)(void*, const void*);
std::atomic<BspDraw> g_bspDraw{nullptr};
std::atomic<unsigned> g_bspSamples{0};
bool SelectedWorld(const char* name) {
    try {
        const auto active = custommaps::Active();
        return !active.empty() && ("maps/mp/" + active + ".d3dbsp") == name;
    } catch (...) {
        return false;
    }
}
void DrawBsp(void* iterator, const void* context) {
    const DWORD saved = GetLastError();
    uintptr_t world = 0, worldName = 0;
    char worldText[64]{};
    if (g_bspSamples.load() < 24 &&
        safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x10C77870), &world, 8) && world &&
        safemem::ReadBytes(reinterpret_cast<void*>(world), &worldName, 8) &&
        safemem::ReadString(reinterpret_cast<const char*>(worldName), worldText,
                            sizeof(worldText)) &&
        SelectedWorld(worldText) && g_bspSamples.fetch_add(1) < 24) {
        uintptr_t state = 0, material = 0, technique = 0, name = 0;
        char mat[160]{}, tech[160]{};
        unsigned type = ~0u;
        unsigned char layout = 255;
        if (safemem::ReadBytes(static_cast<const char*>(context) + 8, &state, 8) && state) {
            safemem::ReadBytes(reinterpret_cast<void*>(state + 0x1138), &material, 8);
            safemem::ReadBytes(reinterpret_cast<void*>(state + 0x1140), &technique, 8);
            if (material && safemem::ReadBytes(reinterpret_cast<void*>(material), &name, 8))
                safemem::ReadString(reinterpret_cast<const char*>(name), mat, sizeof(mat));
            if (technique) {
                safemem::ReadBytes(reinterpret_cast<void*>(technique), &name, 8);
                safemem::ReadString(reinterpret_cast<const char*>(name), tech, sizeof(tech));
                safemem::ReadBytes(reinterpret_cast<void*>(technique + 8), &type, 4);
                safemem::ReadBytes(reinterpret_cast<void*>(technique + 0x9C), &layout, 1);
            }
            LOG_INFO(
                "Render",
                "native BSP dispatch material='%s' technique='%s' type=%u layout=%u acceptedLayout=%d",
                mat, tech, type, layout, layout >= 32 && layout <= 38);
        }
    }
    SetLastError(saved);
    g_bspDraw.load()(iterator, context);
}

void DrawSurfaces(const void* command) {
    const DWORD saved = GetLastError();
    uintptr_t world = 0, name = 0, view = 0, visibility = 0;
    unsigned mask = 0;
    char text[64]{};
    bool capture = false;
    if (safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x10C77870), &world, 8) && world &&
        safemem::ReadBytes(reinterpret_cast<void*>(world), &name, 8) &&
        safemem::ReadString(reinterpret_cast<const char*>(name), text, sizeof(text)) &&
        SelectedWorld(text)) {
        const auto frame = g_frames.fetch_add(1);
        capture = frame < 2400 && frame % 120 == 0;
        if (capture) {
            safemem::ReadBytes(command, &view, 8);
            safemem::ReadBytes(reinterpret_cast<void*>(world + 0x40B8), &visibility, 8);
            if (visibility)
                safemem::ReadBytes(reinterpret_cast<void*>(visibility), &mask, 4);
        }
    }
    SetLastError(saved);
    g_draw.load()(command);
    const DWORD resultError = GetLastError();
    if (capture && view) {
        unsigned words = 0, draws[8]{};
        uintptr_t data = 0, backend = 0, transient = 0;
        unsigned transientMask = 0;
        safemem::ReadBytes(reinterpret_cast<void*>(view + 0x37B0), &words, 4);
        safemem::ReadBytes(reinterpret_cast<void*>(view + 0x37B8), &data, 8);
        if (data)
            safemem::ReadBytes(reinterpret_cast<void*>(data), draws, sizeof(draws));
        safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0x11108AE8), &backend, 8);
        if (backend) {
            safemem::ReadBytes(reinterpret_cast<void*>(backend + 0x17317C), &transientMask, 4);
            safemem::ReadBytes(reinterpret_cast<void*>(backend + 0x173248), &transient, 8);
        }
        LOG_INFO(
            "Render",
            "BSP camera visibility=%08X opaqueWords=%u data=%p draw=%08X,%08X,%08X,%08X transientMask=%08X transient=%p",
            mask, words, reinterpret_cast<void*>(data), draws[0], draws[1], draws[2], draws[3],
            transientMask, reinterpret_cast<void*>(transient));
    }
    SetLastError(resultError);
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
        SelectedWorld(text)) {
        // The engine already uses this conservative fallback when an Umbra
        // query fails. It fills the native surface/light visibility masks and
        // masks unused tail bits; downstream draw and frustum processing stay native.
        reinterpret_cast<void (*)(unsigned)>(g_base + replay::UmbraSetAllVisible.rva)(view);
        customglass::HideBroken(world, view);
        if (!g_reported.exchange(true))
            LOG_INFO(
                "Render",
                "selected custom map has no Umbra tome: enabled native conservative visibility fallback view=%u",
                view);
    }
    SetLastError(saved);
    g_query.load()(command);
}
}
namespace customrender {
hook::Status Install(uintptr_t base) {
    if (g_query.load() && g_draw.load() && g_bspDraw.load())
        return hook::Status::Installed;
    g_base = base;
    const auto& fallback = replay::UmbraSetAllVisible;
    unsigned char bytes[64]{};
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + fallback.rva), bytes, fallback.size) ||
        std::memcmp(bytes, fallback.bytes, fallback.size) != 0)
        return hook::Status::NotReady;
    const auto& query = replay::UmbraQueryStaticVisibilityCmd;
    const auto result = hook::Install(reinterpret_cast<void*>(base + query.rva), &QueryVisibility,
                                      query.bytes, query.size, g_query);
    if (result != hook::Status::Installed)
        return result;
    const auto& draw = replay::AddBspDrawSurfacesCamera;
    const auto drawn = hook::Install(reinterpret_cast<void*>(base + draw.rva), &DrawSurfaces,
                                     draw.bytes, draw.size, g_draw);
    if (drawn != hook::Status::Installed)
        return drawn;
    const auto& bsp = replay::DrawBspSurf;
    return hook::Install(reinterpret_cast<void*>(base + bsp.rva), &DrawBsp, bsp.bytes, bsp.size,
                         g_bspDraw);
}
}
