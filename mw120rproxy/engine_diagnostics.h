#pragma once
#include "inline_hook.h"
#include <windows.h>
namespace enginediag
{
    bool Initialize(HMODULE self);
    hook::Status Install(uintptr_t base);
}
