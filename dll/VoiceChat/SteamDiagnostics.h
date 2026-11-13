#pragma once
#include <Windows.h>

namespace SteamDiagnostics {
    // Opt-in sampler; optional peer trace observes the game's existing sends.
    void Start();
    void Stop();
    // Install before retaining SDK interfaces. The hook drains our worker before
    // the game's SteamAPI_Shutdown invalidates them.
    bool GuardSteamShutdown(HMODULE sdk);
    bool IsSteamShuttingDown();
}
