#pragma once
#include <atomic>
#include <cstdint>

// Lean shared runtime state for mw120rproxy (header-only inline atomics).
namespace state {
enum class HookState { Idle, WaitingForSignal, SignalSeen, Installed, Failed, PrologueMismatch };

inline std::atomic<int> hookState{(int)HookState::Idle};
inline std::atomic<bool> variantHooked{false};
inline std::atomic<uint64_t> dvarsSeen{0};
inline std::atomic<uint64_t> boolDvarsSeen{0};
inline std::atomic<uintptr_t> moduleBase{0};
inline std::atomic<uint32_t> imageSize{0};
inline std::atomic<bool> luaHooked{false};
inline std::atomic<uint32_t> luaChunks{0};
}
