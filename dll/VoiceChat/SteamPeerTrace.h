#pragma once
#include <cstdint>

namespace SteamDiagnostics::PeerTrace {
    // Call only from the diagnostic worker, after MinHook initialization.
    bool Install(uintptr_t gameBase, uintptr_t networkingInterface);
    bool IsPinned();
    void Flush(void (*log)(const char*, ...));
}
