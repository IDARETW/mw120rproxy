#pragma once
#include <windows.h>
#include <cstdint>
#include "inline_hook.h"

namespace offlineauth {
// Disk-only preparation on the worker, before publishing startup readiness.
bool Prepare(HMODULE self);

hook::Status Install(uintptr_t base);
void LogSnapshot();
}
