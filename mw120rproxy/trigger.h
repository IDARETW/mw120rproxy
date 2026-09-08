#pragma once
#include <cstdint>
namespace trigger
{
    using InstallFn = bool(*)();
    // Hooks user32, never the game's IAT. Delivers one signal after the successful
    // bitmap-100 load from the verified game callsite. Zero callsite is for host tests.
    bool InstallLoadImageTrigger(InstallFn fn, uintptr_t imageBase, uintptr_t splashReturnRva);
}
