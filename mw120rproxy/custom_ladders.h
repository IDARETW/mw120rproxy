#pragma once
#include "inline_hook.h"
#include <filesystem>
namespace customladders {
hook::Status Install(uintptr_t base);
void Load(const std::filesystem::path& directory);
}
