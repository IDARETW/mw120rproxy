#include "fastfile_diagnostics.h"
#include "game.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Check(bool value, const char* message) {
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void* expectedHandle;
const char* expectedName;
bool result;
unsigned calls;
bool __fastcall NativeFixture(void* handle, const char* name) {
    Check(GetLastError() == 0x1234, "entry last error preserved");
    Check(handle == expectedHandle && name == expectedName,
          "original argument pointers forwarded exactly");
    ++calls;
    if (handle) {
        const uint32_t index = result ? 37 : 0xFEFFFFFF;
        const uint64_t cache = result ? 0x1122334455667788 : 0;
        memcpy(handle, &index, 4);
        memcpy(static_cast<char*>(handle) + 8, &cache, 8);
    }
    SetLastError(0x5678);
    return result;
}
}

int main() {
    log120r::Init(GetModuleHandleW(nullptr), false);
    const auto size = game::kDbFileOpenRVA + 0x1000;
    auto* image =
        static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
    Check(image != nullptr, "reserve sparse target image");
    auto* page = image + (game::kDbFileOpenRVA & ~uintptr_t{0xFFF});
    Check(VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr,
          "commit function page");
    Check(fastfilediag::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::NotReady,
          "mismatched target bytes do not install");
    auto* target = image + game::kDbFileOpenRVA;
    memcpy(target, game::kDbFileOpenPrologue, sizeof(game::kDbFileOpenPrologue));
    // Undo the exact Replay prologue and tail-call a fixture that validates the ABI.
    const unsigned char tail[] = {0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48,
                                  0x83, 0xC4, 0x20, 0x5F, 0xFF, 0x25, 0,    0,    0,    0};
    memcpy(target + sizeof(game::kDbFileOpenPrologue), tail, sizeof(tail));
    auto native = &NativeFixture;
    memcpy(target + sizeof(game::kDbFileOpenPrologue) + sizeof(tail), &native, sizeof(native));
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    Check(fastfilediag::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "checked hook installs");
    Check(fastfilediag::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "repeat install is idempotent");
    auto open = reinterpret_cast<bool(__fastcall*)(void*, const char*)>(target);
    alignas(8) unsigned char handle[24];
    for (const char* name :
         {"mp_shipment.ff", "ww_mp_hackney_am.ff", "", static_cast<const char*>(nullptr)}) {
        for (bool opened : {true, false}) {
            memset(handle, 0xCC, sizeof(handle));
            expectedHandle = handle;
            expectedName = name;
            result = opened;
            SetLastError(0x1234);
            const bool actual = open(handle, name);
            const DWORD lastError = GetLastError();
            Check(actual == result && lastError == 0x5678,
                  "native result and exit last error preserved");
            uint32_t index;
            uint64_t cache;
            memcpy(&index, handle, 4);
            memcpy(&cache, handle + 8, 8);
            Check(index == (result ? 37u : 0xFEFFFFFFu), "native FileStream index preserved");
            Check(cache == (result ? 0x1122334455667788ull : 0ull), "native DCache ID preserved");
            Check(handle[4] == 0xCC && handle[7] == 0xCC && handle[16] == 0xCC &&
                      handle[23] == 0xCC,
                  "observer leaves padding and adjacent memory unchanged");
        }
    }
    expectedHandle = nullptr;
    expectedName = reinterpret_cast<const char*>(1);
    result = false;
    SetLastError(0x1234);
    Check(!open(nullptr, expectedName),
          "unreadable diagnostic input still forwarded to native fixture");
    Check(GetLastError() == 0x5678 && calls == 9, "exactly one original call per request");
    puts(
        "PASS: native DB open prologue, mismatch/retry, exact pointer forwarding, success/failure handles, last-error state and unreadable diagnostic inputs");
    // Installed detours have process lifetime, matching the proxy.
    return 0;
}
