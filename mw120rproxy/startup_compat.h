#pragma once
#include <cstdint>
namespace startup
{
    // Apply during process attach, before the executable's CRT initializers run.
    // This GDI clone compatibility repair is independent of post-splash game hooks.
    bool ApplyGdiCompatibility(uintptr_t imageBase);
}
