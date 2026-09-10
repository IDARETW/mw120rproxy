// MW2019 1.20 Replay: minimal DllMain and checked startup hooks.
#include <windows.h>
#include <mutex>
#include "proxy.h"
#include "log.h"
#include "logger.h"
#include "config.h"
#include "dvar_patches.h"
#include "trigger.h"
#include "state.h"
#include "game.h"
#include "custom_maps.h"
#include "file_open_hook.h"
#include "diagnostics.h"
#include "startup_compat.h"
#include "offline_auth.h"
#include "fastfile_diagnostics.h"
#include "developer_ui.h"
#include "custom_map_loader.h"
#include "engine_diagnostics.h"
#include "custom_physics.h"
#include "custom_collision.h"
#include "custom_render.h"
#include "custom_omnvars.h"
#include "custom_map_ui.h"
#include "custom_images.h"
#include "custom_audio.h"
#include "custom_surfaces.h"
#include "custom_door_ui.h"
#include "custom_ladders.h"
#include "custom_glass.h"
#include "noclip.h"

namespace {
HMODULE g_self = nullptr;
uintptr_t g_base = 0;
config::Settings g_config;
std::mutex g_installLock;
bool g_finished = false;
bool g_triggerArmed = false;
bool g_gdiCompatibility = false;
std::atomic<bool> g_splashSeen{false};
std::atomic<bool> g_ready{false};

bool CheckTarget(uintptr_t base, uint32_t& size) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"game_dx12_ship_replay.exe") != 0)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp != 0x5E9BAF80 || nt->OptionalHeader.SizeOfImage != 0x1324B000)
        return false;
    size = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool DoInstallHooks() {
    std::lock_guard<std::mutex> lock(g_installLock);
    if (g_finished)
        return true;
    if (!g_splashSeen.load(std::memory_order_acquire))
        return false;
    auto status = dvars::InstallHook(g_base, g_config.hookBool, g_config.hookVariant);
    if (status == hook::Status::Installed && g_config.offlineAuth)
        status = offlineauth::Install(g_base);
    if (status == hook::Status::Installed && g_config.engineDiagnostics)
        status = enginediag::Install(g_base);
    if (status == hook::Status::Installed && g_config.fastfileDiagnostics)
        status = fastfilediag::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customloader::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customphysics::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customcollision::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customrender::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customomnvars::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customladders::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customglass::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customdoorui::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customaudio::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customsurfaces::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = custommapui::Install(g_base);
    if (status == hook::Status::Installed && g_config.developerUI)
        status = noclip::Install(g_base);
    if (status == hook::Status::Installed && g_config.developerUI)
        status = developerui::Install(g_base);
    // Install the asset lookup detour after consumers validate its original
    // prologue. They still call the same entry and receive normal passthrough.
    if (status == hook::Status::Installed && g_config.customMapLoader)
        status = customimages::Install(g_base);
    if (status == hook::Status::Installed && g_config.customMapFileMonitor)
        status = fileopen::Install(g_base, g_config.customMapFileRedirect);
    if (status == hook::Status::NotReady)
        return false;
    g_finished = true;
    state::hookState.store((int)(status == hook::Status::Installed ? state::HookState::Installed
                                                                   : state::HookState::Failed));
    if (status == hook::Status::Installed)
        LOG_INFO("Core", "enabled startup hooks installed; waiting for game registration");
    else
        LOG_ERR("Core",
                "hook installation failed; inspect preceding error (not reported as installed)");
    return true;
}

bool OnSplashLoaded() {
    g_splashSeen.store(true, std::memory_order_release);
    state::hookState.store((int)state::HookState::SignalSeen);
    if (g_ready.load(std::memory_order_acquire))
        DoInstallHooks();
    return true;
}

DWORD WINAPI StartupMonitor(LPVOID) {
    const ULONGLONG start = GetTickCount64();
    while (!g_splashSeen.load(std::memory_order_acquire)) {
        if (GetTickCount64() - start >= 120000) {
            LOG_ERR("Trigger",
                    "game splash not observed; no game hooks installed (no polling fallback)");
            return 0;
        }
        Sleep(10);
    }
    const ULONGLONG installStart = GetTickCount64();
    while (!DoInstallHooks()) {
        if (GetTickCount64() - installStart >= 15000) {
            state::hookState.store((int)state::HookState::PrologueMismatch);
            LOG_ERR("Core", "post-splash target bytes do not match 1.20 Replay");
            return 0;
        }
        Sleep(10);
    }
    for (int i = 0; i < 6; ++i) {
        Sleep(5000);
        LOG_INFO(
            "Startup",
            "post-splash=%ds hook_state=%d bool_calls=%llu variant_calls=%llu (menu state requires an in-game check)",
            (i + 1) * 5, state::hookState.load(), state::boolDvarsSeen.load(),
            state::dvarsSeen.load());
        if (g_config.offlineAuth)
            offlineauth::LogSnapshot();
    }
    return 0;
}

DWORD WINAPI InitThread(LPVOID) {
    // Hooks and callbacks have process lifetime. Do not permit FreeLibrary to
    // unload the proxy underneath a running detour or exception observer.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&InitThread), &pinned);
    try {
        g_config = config::Load(g_self);
        log120r::Init(g_self, g_config.console);
        LOG_INFO(
            "Core",
            "mw120rproxy custom-map-authoring-v54-native-world-collision-test 2026-09-09; target 1.20.4.7623265-replay");
        LOG_INFO(
            "Diagnostics",
            "exception call chains: mw120rproxy.exceptions.log and per-run mw120rproxy.crash.*.log; native errors: mw120rproxy.engine.log");
        LOG_INFO("Startup", "GDI clone compatibility repair at RVA 0x3061A0: %s",
                 g_gdiCompatibility ? "applied" : "FAILED (target bytes/protection mismatch)");
        LOG_INFO(
            "Core",
            "config: hook_bool=%d hook_variant=%d offline_auth=%d map_monitor=%d map_redirect=%d trigger=%s",
            (int)g_config.hookBool, (int)g_config.hookVariant, (int)g_config.offlineAuth,
            (int)g_config.customMapFileMonitor, (int)g_config.customMapFileRedirect,
            g_config.hookTrigger.c_str());
        proxy::Preload();
        LOG_INFO("Core", "fastfile_diagnostics=%d", int(g_config.fastfileDiagnostics));
        LOG_INFO("Core", "developer_ui=%d custom_map_loader=%d", int(g_config.developerUI),
                 int(g_config.customMapLoader));
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uint32_t imageSize = 0;
        if (!CheckTarget(g_base, imageSize)) {
            LOG_WARN("Core", "host is not the supported Replay executable; XInput forwarding only");
            return 0;
        }
        state::moduleBase.store(g_base);
        state::imageSize.store(imageSize);
        if (g_config.engineDiagnostics && !enginediag::Initialize(g_self))
            LOG_ERR("Diagnostics", "cannot open mw120rproxy.engine.log (%lu)", GetLastError());
        if (g_config.offlineAuth && (!g_config.hookBool || !offlineauth::Prepare(g_self))) {
            state::hookState.store((int)state::HookState::Failed);
            LOG_ERR(
                "Auth",
                "local identity preparation failed or hook_bool=0; check mw120rproxy.identity.ini (must contain a nonzero decimal xuid)");
            return 0;
        }
        if (g_config.hookBool || g_config.hookVariant || g_config.offlineAuth ||
            g_config.fastfileDiagnostics || g_config.customMapFileMonitor || g_config.developerUI ||
            g_config.customMapLoader || g_config.engineDiagnostics) {
            dvars::InitDefaults(false);
            state::hookState.store((int)state::HookState::WaitingForSignal);
            if (!g_triggerArmed) {
                state::hookState.store((int)state::HookState::Failed);
                LOG_ERR("Trigger", "user32 splash hook unavailable; no game hooks will install");
                return 0;
            }
            LOG_INFO(
                "Trigger",
                "user32 LoadImageA armed during attach; waiting for bitmap 100 at game RVA 0x1BD3479");
            if (g_config.hookTrigger != "loadimage")
                LOG_WARN("Trigger", "hook_trigger=%s ignored; splash gate is mandatory",
                         g_config.hookTrigger.c_str());
            g_ready.store(true, std::memory_order_release);
            HANDLE monitor = CreateThread(nullptr, 0, StartupMonitor, nullptr, 0, nullptr);
            if (monitor)
                CloseHandle(monitor);
            else
                LOG_ERR("Core", "startup monitor thread failed (%lu)", GetLastError());
        }
        // Package IO must not delay arming the splash gate.
        custommaps::Initialize(g_self);
    } catch (const std::exception& e) {
        LOG_ERR("Core", "initialization failed: %s", e.what());
    }
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uint32_t imageSize = 0;
        if (CheckTarget(g_base, imageSize)) {
            // Before the EXE entry point checks its previous-run flag. Only the
            // two observed crash markers beside this exact supported executable.
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, path, MAX_PATH)) {
                wchar_t* tail = wcsrchr(path, L'\\');
                if (tail)
                    for (const auto* marker : {L"__game_dx12_ship_replay", L"__ModernWarfare"}) {
                        wcscpy_s(tail + 1, MAX_PATH - size_t(tail + 1 - path), marker);
                        WIN32_FILE_ATTRIBUTE_DATA file{};
                        if (GetFileAttributesExW(path, GetFileExInfoStandard, &file) &&
                            !(file.dwFileAttributes &
                              (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                            !file.nFileSizeHigh && file.nFileSizeLow <= 16)
                            DeleteFileW(path);
                    }
            }
            state::moduleBase.store(g_base);
            state::imageSize.store(imageSize);
            diagnostics::Initialize(module);
            g_gdiCompatibility = startup::ApplyGdiCompatibility(g_base);
            g_triggerArmed = trigger::InstallLoadImageTrigger(&OnSplashLoaded, g_base, 0x1BD347F);
        }
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
