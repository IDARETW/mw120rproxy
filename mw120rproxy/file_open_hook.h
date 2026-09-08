#pragma once

#include <cstdint>
#include "inline_hook.h"

namespace fileopen
{
    // Install the 1.20 fastfile request monitor. The redirect option remains off by default.
    // Returns false while the checked target prologue is not ready.
    hook::Status Install(uintptr_t moduleBase, bool redirect);
}
