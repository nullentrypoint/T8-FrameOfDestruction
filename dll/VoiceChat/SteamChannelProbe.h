#pragma once
#include "IdentitySession.h"
#include <memory>

namespace VoiceChat {
class SteamChannelProbe {
public:
    using Log = void(*)(const char*, ...);
    SteamChannelProbe(Log log, uintptr_t userInitializerRva);
    ~SteamChannelProbe();
    SteamChannelProbe(const SteamChannelProbe&) = delete;
    SteamChannelProbe& operator=(const SteamChannelProbe&) = delete;
    void Update(const IdentitySnapshot& snapshot, const IdentitySession& identities);
    void Stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
