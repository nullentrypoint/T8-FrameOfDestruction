#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace SteamDiagnostics {
struct PeerCounter {
    uint64_t peer = 0;
    uintptr_t callsite = 0;
    int channel = 0;
    int mode = 0;
    uint64_t calls = 0;
    uint64_t accepted = 0;
    uint64_t bytes = 0;
};

// Fixed storage and no blocking/file I/O on the sending thread.
class PeerCounters {
    SRWLOCK lock = SRWLOCK_INIT;
    std::array<PeerCounter, 64> entries{};
    std::atomic<uint64_t> dropped{0};
public:
    void Record(uint64_t peer, uintptr_t callsite, int channel, int mode, uint32_t bytes, bool accepted) {
        if (!TryAcquireSRWLockExclusive(&lock)) { ++dropped; return; }
        for (auto& entry : entries) {
            if (!entry.calls || (entry.peer == peer && entry.callsite == callsite &&
                entry.channel == channel && entry.mode == mode)) {
                entry.peer = peer;
                entry.callsite = callsite;
                entry.channel = channel;
                entry.mode = mode;
                ++entry.calls;
                entry.accepted += accepted ? 1 : 0;
                entry.bytes += bytes;
                ReleaseSRWLockExclusive(&lock);
                return;
            }
        }
        ++dropped;
        ReleaseSRWLockExclusive(&lock);
    }

    std::array<PeerCounter, 64> Snapshot(uint64_t& lost) {
        AcquireSRWLockExclusive(&lock);
        const auto copy = entries;
        ReleaseSRWLockExclusive(&lock);
        lost = dropped.load();
        return copy;
    }
};
}
