#include "../stdafx.h"
#include "SteamPeerTrace.h"
#include "PeerCounters.h"
#include <intrin.h>
#ifdef T8_PEER_TRACE_TEST
#include <cassert>
#endif

namespace SteamDiagnostics::PeerTrace {
namespace {
    using Send = bool(__fastcall*)(void*, uint64_t, const void*, uint32_t, int, int);
    Send original = nullptr;
    uintptr_t executable = 0;
    std::atomic<bool> pinned{false};
    PeerCounters counters;
    std::array<PeerCounter, 64> previous{};
    uint64_t previousDropped = 0;
    std::atomic<uint64_t> totalCalls{0};
    std::atomic<uint64_t> knownSiteCalls{0};
    unsigned heartbeat = 0;

    bool Read(uintptr_t address, void* destination, size_t size) {
        SIZE_T count = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            destination, size, &count) && count == size;
    }

    bool __fastcall SendHook(void* self, uint64_t remote, const void* data,
        uint32_t size, int mode, int channel) {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const auto site = caller - executable;
        ++totalCalls;
        const bool result = original(self, remote, data, size, mode, channel);
        if (site == 0x698887E || site == 0x6988901)
            ++knownSiteCalls;
        // Include unanticipated callers: otherwise an empty log cannot distinguish
        // an unused method from an incomplete static call-site filter.
        counters.Record(remote, caller, channel, mode, size, result);
        return result;
    }

    bool Match(uintptr_t address, const unsigned char* expected, size_t size) {
        unsigned char bytes[32]{};
        return size <= sizeof(bytes) && Read(address, bytes, size) && memcmp(bytes, expected, size) == 0;
    }
}

bool Install(uintptr_t base, uintptr_t networkingInterface) {
    if (pinned.load()) return true;
    // Validate actual argument setup and indirect calls, not just their RVAs.
    const unsigned char siteA[] = {
        0x48,0x8B,0x08, 0x48,0x8B,0x57,0x10, 0x48,0x8B,0x01, 0xFF,0x10};
    const unsigned char siteB[] = {
        0x48,0x8B,0x53,0x10, 0x48,0x8B,0x08, 0xC7,0x44,0x24,0x20,0,0,0,0,
        0x48,0x8B,0x01, 0xFF,0x10};
    if (!Match(base + 0x6988872, siteA, sizeof(siteA)) ||
        !Match(base + 0x69888ED, siteB, sizeof(siteB))) return false;
    uintptr_t vtable = 0, method = 0;
    if (!Read(networkingInterface, &vtable, sizeof(vtable)) ||
        !Read(vtable, &method, sizeof(method))) return false;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(method), &owner) || owner != GetModuleHandleW(L"steamclient64.dll")) return false;
    // Match the inspected client's slot as well as the validated game context.
    if (method - reinterpret_cast<uintptr_t>(owner) != 0x71E600) return false;
    auto target = reinterpret_cast<void*>(method);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&SendHook), reinterpret_cast<void**>(&original)) != MH_OK)
        return false;
    // A process-lifetime research hook avoids racing DLL unload against calls
    // that have entered the detour but not yet returned from Steam.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&SendHook), &self)) {
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    pinned.store(true);
    executable = base;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        original = nullptr;
        return false; // Module remains pinned even on enable failure.
    }
    return true;
}

bool IsPinned() { return pinned.load(); }

void Flush(void (*log)(const char*, ...)) {
    uint64_t dropped = 0;
    const auto snapshot = counters.Snapshot(dropped);
    for (size_t i = 0; i < snapshot.size(); ++i) {
        const auto& entry = snapshot[i];
        if (entry.calls == previous[i].calls) continue;
        const auto site = entry.callsite - executable;
        const bool known = site == 0x698887E || site == 0x6988901;
        log("peer-send candidate=%llu channel=%d mode=%d callerVA=0x%llX knownGameSite=%d calls=%llu accepted=%llu bytes=%llu",
            static_cast<unsigned long long>(entry.peer), entry.channel, entry.mode,
            static_cast<unsigned long long>(entry.callsite), known, static_cast<unsigned long long>(entry.calls),
            static_cast<unsigned long long>(entry.accepted), static_cast<unsigned long long>(entry.bytes));
    }
    if (dropped != previousDropped) log("peer-send dropped-observations=%llu", static_cast<unsigned long long>(dropped));
    previous = snapshot;
    previousDropped = dropped;
    if (heartbeat++ % 10 == 0) {
        log("peer-trace heartbeat totalCalls=%llu knownSiteCalls=%llu dropped=%llu pinned=%d",
            static_cast<unsigned long long>(totalCalls.load()), static_cast<unsigned long long>(knownSiteCalls.load()),
            static_cast<unsigned long long>(dropped), IsPinned());
    }
}

#ifdef T8_PEER_TRACE_TEST
namespace {
    void* expectedSelf = nullptr;
    const void* expectedData = nullptr;
    volatile unsigned fakeCalls = 0;
    __declspec(noinline) bool __fastcall FakeSend(void* self, uint64_t remote,
        const void* data, uint32_t size, int mode, int channel) {
        assert(self == expectedSelf && data == expectedData);
        assert(remote == 0x0110000101234567ULL && size == 64 && channel == 789);
        fakeCalls = fakeCalls + 1;
        return mode == 3;
    }
}

// Runs only in the standalone smoke executable; never in a game build.
void TestDetourForwarding() {
    unsigned char payload[64]{};
    expectedSelf = &payload[0];
    expectedData = payload;
    assert(MH_Initialize() == MH_OK);
    auto target = reinterpret_cast<void*>(&FakeSend);
    assert(MH_CreateHook(target, reinterpret_cast<void*>(&SendHook), reinterpret_cast<void**>(&original)) == MH_OK);
    assert(MH_EnableHook(target) == MH_OK);
    Send volatile invoke = &FakeSend;
    assert(invoke(expectedSelf, 0x0110000101234567ULL, payload, 64, 3, 789));
    assert(!invoke(expectedSelf, 0x0110000101234567ULL, payload, 64, 0, 789));
    assert(MH_DisableHook(target) == MH_OK);
    assert(MH_RemoveHook(target) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    assert(fakeCalls == 2 && totalCalls.load() == 2 && knownSiteCalls.load() == 0);
    uint64_t dropped = 0, calls = 0, accepted = 0, bytes = 0;
    for (const auto& entry : counters.Snapshot(dropped)) {
        calls += entry.calls;
        accepted += entry.accepted;
        bytes += entry.bytes;
    }
    assert(dropped == 0 && calls == 2 && accepted == 1 && bytes == 128);
    original = nullptr;
}
#endif
}
