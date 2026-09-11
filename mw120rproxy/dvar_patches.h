#pragma once
#include <cstdint>
#include "inline_hook.h"

// Offline bool-dvar override + full dvar trace for 1.20.4.7623265-replay. Lean port of
// mw164proxy's dvar_patches (itself a port of mw169proxy's, which is a port of mw124proxy's; the
// web-panel List/Upsert/LiveSet surface is dropped). No-ops until game::kDvarRegisterBoolRVA is filled.
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
