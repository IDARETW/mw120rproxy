#include "trigger.h"
#include "inline_hook.h"
#include "logger.h"
#include "safemem.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>

namespace trigger {
namespace {
using LoadImageFn = HANDLE(WINAPI*)(HINSTANCE, LPCSTR, UINT, int, int, UINT);
std::atomic<LoadImageFn> g_original{nullptr};
InstallFn g_install = nullptr;
uintptr_t g_imageBase = 0;
uintptr_t g_splashReturnRva = 0;
std::atomic<bool> g_delivered{false};

constexpr uint8_t kCurrent[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56,
                                0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr uint8_t kLegacy[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                               0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30};

HANDLE WINAPI Detour(HINSTANCE instance, LPCSTR name, UINT type, int cx, int cy, UINT flags) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    HANDLE result = g_original.load(std::memory_order_acquire)(instance, name, type, cx, cy, flags);
    const DWORD error = GetLastError();
    const bool splash = reinterpret_cast<uintptr_t>(instance) == g_imageBase &&
                        name == MAKEINTRESOURCEA(100) && type == IMAGE_BITMAP &&
                        (g_splashReturnRva == 0 || caller == g_imageBase + g_splashReturnRva);
    if (splash && result && !g_delivered.exchange(true)) {
        // Signal only AFTER the real game splash load returns successfully.
        LOG_INFO("Trigger",
                 "game splash bitmap 100 loaded; caller RVA=0x%llX; releasing game-hook gate",
                 (unsigned long long)(caller - g_imageBase));
        if (g_install)
            g_install();
    }
    SetLastError(error);
    return result;
}
}

bool InstallLoadImageTrigger(InstallFn install, uintptr_t imageBase, uintptr_t splashReturnRva) {
    g_install = install;
    g_imageBase = imageBase;
    g_splashReturnRva = splashReturnRva;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    // Called during attach: dependencies are already loaded; do not LoadLibrary.
    auto* target = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "LoadImageA")) : nullptr;
    uint8_t actual[sizeof(kCurrent)]{};
    if (!target || !safemem::ReadBytes(target, actual, sizeof(actual)))
        return false;
    const uint8_t* expected = nullptr;
    size_t size = 0;
    if (std::memcmp(actual, kCurrent, sizeof(kCurrent)) == 0) {
        expected = kCurrent;
        size = sizeof(kCurrent);
    } else if (std::memcmp(actual, kLegacy, sizeof(kLegacy)) == 0) {
        expected = kLegacy;
        size = sizeof(kLegacy);
    }
    if (!expected)
        return false;
    return hook::Install(target, &Detour, expected, size, g_original) == hook::Status::Installed;
}
}
