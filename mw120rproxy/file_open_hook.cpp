#include "file_open_hook.h"

#include "custom_maps.h"
#include "game.h"
#include "inline_hook.h"
#include "logger.h"
#include "safemem.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
using FsFOpenFileReadCurrentThread_t = int64_t(__fastcall*)(const char* filename, void* file);

std::atomic<FsFOpenFileReadCurrentThread_t> g_original{nullptr};
std::atomic<bool> g_redirect{false};

bool PrologueMatches(const uint8_t* target, const uint8_t* expected, size_t size) {
    uint8_t actual[32]{};
    return size <= sizeof(actual) && safemem::ReadBytes(target, actual, size) &&
           std::memcmp(actual, expected, size) == 0;
}

int64_t __fastcall Detour(const char* filename, void* file) {
    char request[MAX_PATH]{};
    safemem::ReadString(filename, request, sizeof(request));

    char selectedPath[MAX_PATH]{};
    if (custommaps::ActiveZoneQPath(request, selectedPath, sizeof(selectedPath))) {
        if (g_redirect.load(std::memory_order_relaxed)) {
            LOG_INFO("FileOpen", "redirecting selected zone '%s' to '%s'", request, selectedPath);
            return g_original.load(std::memory_order_acquire)(selectedPath, file);
        }
        LOG_INFO("FileOpen", "selected zone request '%s'; monitor only", request);
    }
    return g_original.load(std::memory_order_acquire)(filename, file);
}
}

namespace fileopen {
hook::Status Install(uintptr_t moduleBase, bool redirect) {
    const bool already = g_original.load(std::memory_order_acquire) != nullptr;
    g_redirect.store(redirect, std::memory_order_relaxed);
    auto* target = reinterpret_cast<void*>(moduleBase + game::kFsFOpenFileReadCurrentThreadRVA);
    const auto status = hook::Install(target, &Detour, game::kFsFOpenFileReadCurrentThreadPrologue,
                                      game::kFsFOpenFileReadCurrentThreadStolen, g_original);
    if (status == hook::Status::Installed && !already)
        LOG_INFO("FileOpen", "file-open monitor installed at RVA 0x%llX; redirect=%d",
                 (unsigned long long)game::kFsFOpenFileReadCurrentThreadRVA, (int)redirect);
    return status;
}
}
