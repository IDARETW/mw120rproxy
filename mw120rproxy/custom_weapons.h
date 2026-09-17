#pragma once
#include "inline_hook.h"
#include <string>

namespace customweapons {
hook::Status Install(uintptr_t base);
bool IsZone(const char* name);
void* ExtendAsset(int type, const char* name, void* source, uintptr_t callerRva = 0);
bool ResolveDiskRead(const char* name, std::string& path);
}
