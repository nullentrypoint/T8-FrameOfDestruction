#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoiceChat {
enum class IdentityReadStatus { Ok, NotReady, UnsupportedBuild, Unreadable, InvalidData, ChangedDuringRead };
const char* IdentityStatusName(IdentityReadStatus status);

struct PlayerIdentity {
    uintptr_t address = 0;
    std::array<uint32_t, 4> rawWords{}; // +8..+0x14: semantics still being researched.
    std::array<std::string, 4> ids; // +0x60, +0x80 (Tekken), +0xA0, +0xC0 (platform-dependent).
    bool operator==(const PlayerIdentity&) const = default;
};

struct IdentitySnapshot {
    IdentityReadStatus status = IdentityReadStatus::NotReady;
    uintptr_t root = 0;
    std::vector<PlayerIdentity> players;
    bool operator==(const IdentitySnapshot&) const = default;
    // Intentionally no active-match/Steam-platform inference from this container.
};

struct IdentityLayout {
    uintptr_t rootRva = 0x9B8CB70;
    uintptr_t vtableRva = 0x855C2C8;
    uintptr_t destructorRva = 0x5BF31F0;
};

using IdentityMemoryRead = bool(*)(void* context, uintptr_t address, void* output, size_t size);

class PlayerIdentityReader {
public:
    explicit PlayerIdentityReader(uintptr_t moduleBase);
    PlayerIdentityReader(uintptr_t moduleBase, IdentityMemoryRead read, void* context, IdentityLayout layout = {});
    IdentitySnapshot Read() const;
private:
    uintptr_t base;
    IdentityMemoryRead readMemory;
    void* context;
    IdentityLayout layout;
    bool ReadBytes(uintptr_t address, void* output, size_t size) const;
    IdentityReadStatus ReadString(uintptr_t address, std::string& output) const;
};
}
