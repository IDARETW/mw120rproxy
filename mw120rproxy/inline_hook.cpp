#include "inline_hook.h"
#include "logger.h"
#include "safemem.h"
#include "thirdparty/minhook/include/MinHook.h"
#include <cstring>

namespace hook::detail {
std::mutex& Mutex() {
    static std::mutex mutex;
    return mutex;
}

Status Prepare(
    void* target, void* detour, const uint8_t* expected, size_t expectedSize, void** trampoline) {
    uint8_t actual[64]{};
    if (!target || !detour || !expected || expectedSize < 5 || expectedSize > sizeof(actual))
        return Status::Failed;
    if (!safemem::ReadBytes(target, actual, expectedSize) ||
        std::memcmp(actual, expected, expectedSize) != 0)
        return Status::NotReady;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERR("Hook", "MinHook initialization failed: %s", MH_StatusToString(init));
        return Status::Failed;
    }
    const MH_STATUS status = MH_CreateHook(target, detour, trampoline);
    if (status != MH_OK) {
        LOG_ERR("Hook", "create @%p failed: %s", target, MH_StatusToString(status));
        return Status::Failed;
    }
    return Status::Installed;
}

bool Enable(void* target) {
    const MH_STATUS status = MH_EnableHook(target);
    if (status == MH_OK)
        return true;
    LOG_ERR("Hook", "enable @%p failed: %s", target, MH_StatusToString(status));
    MH_RemoveHook(target);
    return false;
}
}
