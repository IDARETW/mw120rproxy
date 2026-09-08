#pragma once

#include "inline_hook.h"
#include <cstdint>

namespace fastfilediag
{
    // Observe the native DB open result. Does not replace paths, handles or errors.
    hook::Status Install(uintptr_t moduleBase);
}
