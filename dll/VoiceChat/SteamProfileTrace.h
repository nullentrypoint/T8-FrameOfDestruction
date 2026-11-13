#pragma once
#include <cstdint>

namespace VoiceChat { struct MemoryLayout; }

namespace SteamDiagnostics::ProfileTrace {
    bool Install(uintptr_t gameBase, const VoiceChat::MemoryLayout& layout);
    bool IsPinned();
    void Flush(void (*log)(const char*, ...));
}
