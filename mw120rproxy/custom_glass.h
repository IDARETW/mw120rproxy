#pragma once
#include "inline_hook.h"
#include <filesystem>
namespace customglass {
hook::Status Install(uintptr_t base);
void Load(const std::filesystem::path& directory);
void Clear();
void HideBroken(uintptr_t world, unsigned view);
void PumpEffects();
}
