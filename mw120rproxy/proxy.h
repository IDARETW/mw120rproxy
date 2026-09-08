#pragma once

// Forwarders for the XInput9_1_0.dll exports the game imports. Implemented in
// proxy.cpp and exported under their real names + ordinals via XInput9_1_0.def.
// The real system XInput9_1_0.dll is loaded lazily from System32 on first use.
namespace proxy
{
    // Optional: force the real XInput9_1_0.dll to load now (otherwise lazy).
    void Preload();
}
