#pragma once
#include "inline_hook.h"
namespace custommapui {
hook::Status Install(uintptr_t base);
void SyncSelection(const char* map);
}
