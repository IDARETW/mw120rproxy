#pragma once
#include <filesystem>
#include <string>
namespace customdoors {
using NativeTrace = void (*)(int,
                             void*,
                             const float*,
                             const float*,
                             const float*,
                             const int*,
                             int,
                             int,
                             int,
                             int,
                             const unsigned char*,
                             int);
void Initialize(uintptr_t base, NativeTrace trace);
void Load(const std::filesystem::path& directory);
void Clear();
void Movement(void* pm, void* pml);
void Trace(void* result,
           const float* start,
           const float* end,
           const float* bounds,
           int mask,
           bool melee = false);
void Visibility(uintptr_t world, unsigned view);
void PumpSounds();
std::string Hint();
}
