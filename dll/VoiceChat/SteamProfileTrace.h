#pragma once
#include <cstdint>

namespace SteamDiagnostics::ProfileTrace {
    bool Install(uintptr_t gameBase);
    bool IsPinned();
    void Flush(void (*log)(const char*, ...));
}
