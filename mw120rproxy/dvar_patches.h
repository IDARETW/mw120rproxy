#pragma once
#include <cstdint>
#include "inline_hook.h"

namespace dvars {
// Seed the built-in offline override table (call once at startup). When onlineMpRoute is set, also apply
// the A/B online-MP route block (UI-online dvars on, direct-service dvars off).
void InitDefaults(bool onlineMpRoute, bool luiForceOnline = true);

// Install the Dvar_RegisterBool override hook (+ the Dvar_RegisterVariant trace
// hook when hookVariant). Verifies the decrypted prologue first; returns false
// while .text is still ciphertext so the LoadImageA trigger retries. If the RVA
// is still 0 (unknown) it logs once and returns true (nothing to retry).
hook::Status InstallHook(uintptr_t moduleBase, bool hookBool, bool hookVariant);

// token (encoded hash) -> readable name (dvar_names.h), or nullptr.
const char* ResolveName(const char* token);
}
