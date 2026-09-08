#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace hook {
enum class Status { Installed, NotReady, Failed };
namespace detail {
std::mutex& Mutex();
Status Prepare(
    void* target, void* detour, const uint8_t* expected, size_t expectedSize, void** trampoline);
bool Enable(void* target);
}

// Verify and prepare, publish the original, THEN enable the detour.
// MinHook relocates instructions and adjusts suspended instruction pointers.
// Live trampolines remain allocated until process exit.
template <typename Fn>
Status Install(void* target,
               Fn detour,
               const uint8_t* expected,
               size_t expectedSize,
               std::atomic<Fn>& original) {
    std::lock_guard<std::mutex> lock(detail::Mutex());
    if (original.load(std::memory_order_acquire))
        return Status::Installed;
    void* trampoline = nullptr;
    const Status status = detail::Prepare(target, reinterpret_cast<void*>(detour), expected,
                                          expectedSize, &trampoline);
    if (status != Status::Installed)
        return status;
    original.store(reinterpret_cast<Fn>(trampoline), std::memory_order_release);
    if (detail::Enable(target))
        return Status::Installed;
    original.store(nullptr, std::memory_order_release);
    return Status::Failed;
}
}
