#pragma once

namespace SteamDiagnostics {
    // Opt-in sampler; optional peer trace observes the game's existing sends.
    void Start();
    void Stop();
}
