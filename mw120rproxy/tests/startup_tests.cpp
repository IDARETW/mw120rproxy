#include "inline_hook.h"
#include "dvar_patches.h"
#include "game.h"
#include "trigger.h"
#include "diagnostics.h"
#include "asset_context.h"
#include "replay_symbols.h"
#include "startup_compat.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
void Check(bool value, const char* name) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}
using Fn = int (*)();
constexpr size_t kCount = 128;
std::atomic<Fn> originals[kCount];
Fn functions[kCount]{};
std::atomic<unsigned long long> calls{0};
thread_local size_t active = 0;
std::atomic<bool> stop{false};

int Detour() {
    const auto original = originals[active].load(std::memory_order_acquire);
    Check(original != nullptr, "detour cannot run before original publication");
    return original() + 1;
}

void CompatibilityTest() {
    auto* image = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x320000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "allocate GDI compatibility fixture");
    auto* target = image + game::kGdiCloneBootstrapRVA;
    Check(!startup::ApplyGdiCompatibility(reinterpret_cast<uintptr_t>(image)),
          "unknown startup bytes rejected");
    Check(*target == 0, "mismatched startup is unchanged");
    std::memcpy(target, game::kGdiCloneBootstrapPrologue, sizeof(game::kGdiCloneBootstrapPrologue));
    Check(startup::ApplyGdiCompatibility(reinterpret_cast<uintptr_t>(image)),
          "verified GDI constructor repaired");
    Check(target[0] == 0xC3 && std::memcmp(target + 1, game::kGdiCloneBootstrapPrologue + 1,
                                           sizeof(game::kGdiCloneBootstrapPrologue) - 1) == 0,
          "only one byte modified");
    Check(startup::ApplyGdiCompatibility(reinterpret_cast<uintptr_t>(image)),
          "GDI repair is idempotent");
    reinterpret_cast<void (*)()>(target)();
    VirtualFree(image, 0, MEM_RELEASE);
    std::puts(
        "PASS: exact-version GDI repair, mismatch rejection, one-byte scope, idempotence, executable return");
}

void CaptureProbe() {
    __try {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool SplashInstall() {
    ++calls;
    return true;
}

void ConcurrentHookTest() {
    auto* page = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(page != nullptr, "allocate synthetic executable functions");
    // Each function starts with a RIP-relative read. Returning 41 normally,
    // 42 when hooked exercises relocation as well as concurrent installation.
    for (size_t i = 0; i < kCount; ++i) {
        auto* code = page + i * 64;
        const unsigned char body[] = {0x8B, 0x05, 0x0A, 0, 0, 0, 0xC3};
        std::memcpy(code, body, sizeof(body));
        const int value = 41;
        std::memcpy(code + 16, &value, sizeof(value));
        functions[i] = reinterpret_cast<Fn>(code);
    }
    FlushInstructionCache(GetCurrentProcess(), page, 0x10000);
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i)
        workers.emplace_back([] {
            while (!stop.load()) {
                for (active = 0; active < kCount; ++active) {
                    int result = functions[active]();
                    Check(result == 41 || result == 42, "correct concurrent result");
                    ++calls;
                }
            }
        });
    while (calls.load() < 10000)
        SwitchToThread();
    const unsigned char expected[] = {0x8B, 0x05, 0x0A, 0, 0, 0};
    for (size_t i = 0; i < kCount; ++i) {
        Check(hook::Install(reinterpret_cast<void*>(functions[i]), &Detour, expected,
                            sizeof(expected), originals[i]) == hook::Status::Installed,
              "install concurrent hook");
        // Must not recheck the now-patched original bytes and fail on retry.
        Check(hook::Install(reinterpret_cast<void*>(functions[i]), &Detour, expected,
                            sizeof(expected), originals[i]) == hook::Status::Installed,
              "repeat install is idempotent");
    }
    stop.store(true);
    for (auto& worker : workers)
        worker.join();
    for (active = 0; active < kCount; ++active)
        Check(functions[active]() == 42, "every detour calls relocated original");
    std::printf(
        "PASS: 128 concurrent hooks, four callers, %llu calls, RIP relocation, repeat installation\n",
        calls.load());
    // Trampolines/functions have process lifetime, matching the real proxy.
}

void RejectedHookTest() {
    unsigned char expected[8]{};
    std::atomic<Fn> original{nullptr};
    Check(hook::Install(nullptr, &Detour, expected, sizeof(expected), original) ==
              hook::Status::Failed,
          "invalid arguments fail visibly");
    auto* page = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(page != nullptr, "allocate mismatch fixture");
    std::memset(page, 0x90, 16);
    Check(hook::Install(page, &Detour, expected, sizeof(expected), original) ==
              hook::Status::NotReady,
          "mismatch requests retry");
    Check(!original.load() && page[0] == 0x90,
          "mismatch publishes no original and writes no bytes");
    VirtualFree(page, 0, MEM_RELEASE);
    std::puts("PASS: mismatch and invalid argument rejection");
}

void DvarHookTest() {
    // Real target prologues followed by synthetic function bodies. This tests
    // the proxy's ABI/override/retry logic without executing any game code.
    auto* base = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x1400000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(base != nullptr, "allocate dvar fixture");
    auto* b = base + game::kDvarRegisterBoolRVA;
    std::memcpy(b, game::kDvarRegisterBoolPrologue, game::kDvarRegisterBoolStolen);
    const unsigned char boolBody[] = {0x0F, 0xB6, 0xC2, 0x48, 0x83, 0xC4, 0x60, 0x5F, 0xC3};
    std::memcpy(b + game::kDvarRegisterBoolStolen, boolBody, sizeof(boolBody));
    dvars::InitDefaults(false);
    const auto address = reinterpret_cast<uintptr_t>(base);
    Check(dvars::InstallHook(address, true, true) == hook::Status::NotReady,
          "bool installed while variant not ready");
    auto registerBool = reinterpret_cast<game::Dvar_RegisterBool_t>(b);
    Check(registerBool("MPSSOTQQPM", false, 0, "") == (void*)1, "force_offline_enabled enabled");
    Check(registerBool("LSTQOKLTRN", false, 0, "") == (void*)1, "force_offline_menus enabled");
    Check(registerBool("ui_onlineRequired", true, 0, "") == nullptr, "online requirement disabled");
    Check(registerBool("unrelated_bool", false, 0, "") == nullptr, "unrelated bool preserved");
    Check(registerBool("unrelated_bool", true, 0, "") == (void*)1, "unrelated true preserved");
    auto* v = base + game::kDvarRegisterVariantRVA;
    std::memcpy(v, game::kDvarRegisterVariantPrologue, game::kDvarRegisterVariantStolen);
    const unsigned char variantBody[] = {0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E,
                                         0x5D, 0x48, 0x8B, 0x44, 0x24, 0x38, 0xC3};
    std::memcpy(v + game::kDvarRegisterVariantStolen, variantBody, sizeof(variantBody));
    FlushInstructionCache(GetCurrentProcess(), base, 0x1400000);
    Check(dvars::InstallHook(address, true, true) == hook::Status::Installed,
          "partially installed dvar hooks retry successfully");
    auto registerVariant = reinterpret_cast<game::Dvar_RegisterVariant_t>(v);
    const char description[] = "seventh argument survives";
    bool value = true;
    Check(registerVariant("test_variant", 123, 0, 456, &value, nullptr, description) == description,
          "variant forwards all seven arguments");
    std::puts(
        "PASS: real 1.20 prologue trampolines, offline switches, stock bools, partial retry, seven-argument ABI");
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--compat") == 0) {
        CompatibilityTest();
        return 0;
    }
    if (argc > 2) {
        diagnostics::Initialize(GetModuleHandleW(nullptr));
        uintptr_t offset = 0;
        Check(std::strcmp(replaysymbols::Find(0x161ABE4, offset), "HavokPhysics_AddShapeList") ==
                      0 &&
                  offset == 0x94,
              "verified fault symbol and offset");
        assetcontext::current.active = true;
        assetcontext::current.type = 29;
        strcpy_s(assetcontext::current.name, "maps/mp/mp_test.d3dbsp");
        assetcontext::current.phase = "HavokPhysics_AddShapeList";
        CaptureProbe();
        assetcontext::current = {};
        std::puts("PASS: exception observer preserves SEH handling and emits call chain");
        return 0;
    }
    if (argc == 1) {
        CompatibilityTest();
        RejectedHookTest();
        ConcurrentHookTest();
        DvarHookTest();
    }
    calls.store(0);
    const auto module = GetModuleHandleW(nullptr);
    Check(trigger::InstallLoadImageTrigger(&SplashInstall, reinterpret_cast<uintptr_t>(module), 0),
          "user32 splash gate arms on current Windows");
    LoadImageA(nullptr, "missing-test-image.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    Check(calls.load() == 0, "unrelated LoadImage calls do not release the gate");
    LoadImageA(module, MAKEINTRESOURCEA(101), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    Check(calls.load() == 0, "wrong/missing bitmap does not release the gate");
    HANDLE bitmap =
        LoadImageA(module, MAKEINTRESOURCEA(100), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    Check(bitmap != nullptr && calls.load() == 1, "successful bitmap-100 load releases gate");
    DeleteObject(bitmap);
    bitmap = LoadImageA(module, MAKEINTRESOURCEA(100), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    Check(bitmap != nullptr && calls.load() == 1, "repeat splash preserves image and signals once");
    DeleteObject(bitmap);
    std::puts(
        "PASS: user32 hook, unrelated/missing image rejection, successful splash bitmap, one-shot gate");
    return 0;
}
