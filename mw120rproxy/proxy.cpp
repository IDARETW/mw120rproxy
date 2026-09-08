#include "proxy.h"
#include "log.h"
#include "logger.h"

#include <windows.h>
#include <mutex>
#include <cstdint>

// Transparent forwarder for the *entire* XInput9_1_0.dll export surface. Every
// module in the process that resolves an XInput9_1_0 import (the game, overlays,
// SDKs) binds against us, so we export all 5 of the real DLL's names (and their
// ordinals, via the .def), not just the ones the main exe uses.
//
// We avoid per-function prototypes by forwarding through a uniform 8-register-
// argument thunk. x64 has a single calling convention: args 1-4 arrive in
// RCX/RDX/R8/R9 and 5-8 on the stack. Every XInput9_1_0 function takes <= 8 args
// (in fact <= 3), so passing all eight straight through reproduces the original
// call exactly; the real callee ignores any args it does not declare. The 32-bit
// DWORD results live in EAX, which the uintptr_t return preserves.
//
// Real DLL export set (System32\XInput9_1_0.dll, ordinal base 1):
//   1 DllMain   2 XInputGetCapabilities   3 XInputGetDSoundAudioDeviceGuids
//   4 XInputGetState   5 XInputSetState

namespace
{
    using GenericFn = uintptr_t(WINAPI*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                         uintptr_t, uintptr_t, uintptr_t, uintptr_t);

    struct Real
    {
        HMODULE   mod = nullptr;
        GenericFn DllMain                          = nullptr;
        GenericFn XInputGetCapabilities            = nullptr;
        GenericFn XInputGetDSoundAudioDeviceGuids  = nullptr;
        GenericFn XInputGetState                   = nullptr;
        GenericFn XInputSetState                   = nullptr;
    };

    Real           g_real;
    std::once_flag g_once;

    void Load()
    {
        std::call_once(g_once, []
        {
            wchar_t path[MAX_PATH]{};
            UINT n = GetSystemDirectoryW(path, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return;
            wcsncat_s(path, MAX_PATH, L"\\XInput9_1_0.dll", _TRUNCATE);

            g_real.mod = LoadLibraryW(path); // full path -> the genuine system DLL
            if (!g_real.mod)
            {
                LOG_ERR("Proxy", "failed to load real XInput9_1_0.dll (%lu)", GetLastError());
                return;
            }

            auto get = [](const char* nm) { return (GenericFn)GetProcAddress(g_real.mod, nm); };
            int ok = 0;
        #define RESOLVE(fn) do { g_real.fn = get(#fn); if (g_real.fn) ++ok; } while (0)
            RESOLVE(DllMain);
            RESOLVE(XInputGetCapabilities);
            RESOLVE(XInputGetDSoundAudioDeviceGuids);
            RESOLVE(XInputGetState);
            RESOLVE(XInputSetState);
        #undef RESOLVE
            LOG_INFO("Proxy", "real XInput9_1_0.dll @ %p (%d/5 exports resolved)", (void*)g_real.mod, ok);
        });
    }
}

namespace proxy { void Preload() { Load(); } }

// One uniform thunk per export. The .def maps the real names+ordinals onto these.
#define FORWARD(fn)                                                                  \
    extern "C" uintptr_t WINAPI Proxy_##fn(uintptr_t a, uintptr_t b, uintptr_t c,    \
                                           uintptr_t d, uintptr_t e, uintptr_t f,    \
                                           uintptr_t g, uintptr_t h)                 \
    {                                                                               \
        Load();                                                                     \
        return g_real.fn ? g_real.fn(a, b, c, d, e, f, g, h) : 0;                    \
    }

FORWARD(DllMain)
FORWARD(XInputGetCapabilities)
FORWARD(XInputGetDSoundAudioDeviceGuids)
FORWARD(XInputGetState)
FORWARD(XInputSetState)

#undef FORWARD
