#pragma once
#include <windows.h>
#include <cstdint>
#include "inline_hook.h"

namespace offlineauth
{
    // Disk-only preparation on the worker, before publishing startup readiness.
    bool Prepare(HMODULE self);
    // Game hooks only: called behind the verified LoadImageA splash gate.
    hook::Status Install(uintptr_t base);
    void LogSnapshot();
}
