#include "startup_compat.h"
#include "game.h"
#include "safemem.h"
#include <windows.h>
#include <cstring>

namespace startup {
bool ApplyGdiCompatibility(uintptr_t imageBase) {
    auto* target = reinterpret_cast<uint8_t*>(imageBase + game::kGdiCloneBootstrapRVA);
    uint8_t actual[sizeof(game::kGdiCloneBootstrapPrologue)]{};
    if (!safemem::ReadBytes(target, actual, sizeof(actual)))
        return false;
    // Repeated setup is allowed only when our one-byte patch AND the rest of
    // the exact target prologue match. Never accept arbitrary patched code.
    if (actual[0] == 0xC3 &&
        std::memcmp(actual + 1, game::kGdiCloneBootstrapPrologue + 1, sizeof(actual) - 1) == 0)
        return true;
    if (std::memcmp(actual, game::kGdiCloneBootstrapPrologue, sizeof(actual)) != 0)
        return false;
    DWORD old = 0;
    if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old))
        return false;
    // Target CRT constructor has not run: no instruction is executing here.
    // Matches IW8-1.20-main's attach-time repair of this exact Replay RVA.
    *target = 0xC3;
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), target, 1);
    DWORD unused;
    const BOOL restored = VirtualProtect(target, 1, old, &unused);
    return flushed && restored;
}
}
