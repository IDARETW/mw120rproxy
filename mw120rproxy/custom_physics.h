#pragma once
#include "inline_hook.h"
namespace customphysics {
hook::Status Install(uintptr_t base);
bool OwnsEmptyWorld();
bool OwnsNativeWorld();
bool OwnsCustomWorld();
}
