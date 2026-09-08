#pragma once
#include <cstdint>
namespace trigger {
using InstallFn = bool (*)();

bool InstallLoadImageTrigger(InstallFn fn, uintptr_t imageBase, uintptr_t splashReturnRva);
}
