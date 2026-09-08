#pragma once
#include "inline_hook.h"
#include <cstdint>

namespace customloader
{
    hook::Status Install(uintptr_t base);
    bool Ready();
}
