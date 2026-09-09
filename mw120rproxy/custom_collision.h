#pragma once
#include "inline_hook.h"
namespace customcollision {
hook::Status Install(uintptr_t base);
bool Visible(const float* start, const float* end);
void TraceShot(int world,
               void* trace,
               const float* start,
               const float* end,
               const float* bounds,
               int mask,
               int phase,
               bool detectInside);
}
