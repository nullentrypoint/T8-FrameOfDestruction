#include "../stdafx.h"
#include "SteamProfileTrace.h"
#include "MemoryLayout.h"
#include <array>
#include <atomic>
#ifdef T8_PEER_TRACE_TEST
#include <cassert>
#endif

namespace SteamDiagnostics::ProfileTrace {
namespace {
    // Inspected Windows x64 constructor: RCX=this, RDX=SteamID64,
    // R8=callback object, RAX=this. This is not the public Steam API signature.
    using OpenProfile = void*(__fastcall*)(void*, uint64_t, const void*);
    OpenProfile original = nullptr;
    std::atomic<bool> pinned{false};
    bool installed = false;
    struct Event {
        uint64_t id = 0;
        uint64_t tick = 0;
        std::array<void*, 12> frames{};
        USHORT frameCount = 0;
    };
    SRWLOCK lock = SRWLOCK_INIT;
    std::array<Event, 16> events{};
    size_t count = 0;
    std::atomic<uint64_t> calls{0}, dropped{0};
    unsigned heartbeat = 0;

    void* __fastcall Hook(void* self, uint64_t id, const void* callback) {
        ++calls;
        Event event{};
        event.id = id;
        event.tick = GetTickCount64();
        event.frameCount = CaptureStackBackTrace(1, static_cast<DWORD>(event.frames.size()), event.frames.data(), nullptr);
        if (TryAcquireSRWLockExclusive(&lock)) {
            if (count < events.size()) events[count++] = event;
            else ++dropped;
            ReleaseSRWLockExclusive(&lock);
        } else ++dropped;
        // No file I/O, callback invocation or profile request added here.
        return original(self, id, callback);
    }

    template<size_t N> bool Match(uintptr_t address, const unsigned char (&expected)[N]) {
        unsigned char bytes[N]{};
        SIZE_T copied = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes, N, &copied)
            && copied == N && memcmp(bytes, expected, N) == 0;
    }
}

bool Install(uintptr_t base, const VoiceChat::MemoryLayout& layout) {
    if (installed) return true;
    if (!layout.profileEntry || !layout.profileOverlayCall || !layout.profileDialog) return false;
    const unsigned char prologue[] = {
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,
        0x48,0x83,0xEC,0x40,0x49,0x8B,0xD8,0x48,0x8B,0xEA,0x48,0x8B,0xF9};
    const unsigned char overlayPrefix[] = {0x4C,0x8B,0xC5,0x48,0x8D,0x15};
    const unsigned char overlaySuffix[] = {0xFF,0x90,0xE8,0,0,0};
    const unsigned char dialog[] = {'s','t','e','a','m','i','d',0};
    const auto call = base + layout.profileOverlayCall;
    int32_t displacement = 0;
    SIZE_T copied = 0;
    if (!Match(base + layout.profileEntry, prologue) || !Match(call, overlayPrefix) ||
        !Match(call + 10, overlaySuffix) || !Match(base + layout.profileDialog, dialog) ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(call + 6), &displacement,
            sizeof(displacement), &copied) || copied != sizeof(displacement) ||
        call + 10 + displacement != base + layout.profileDialog) return false;
    auto target = reinterpret_cast<void*>(base + layout.profileEntry);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original)) != MH_OK)
        return false;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Hook), &self)) {
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    pinned.store(true);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    installed = true;
    return true;
}

bool IsPinned() { return pinned.load(); }

void Flush(void (*log)(const char*, ...)) {
    std::array<Event, 16> batch{};
    AcquireSRWLockExclusive(&lock);
    const size_t batchCount = count;
    for (size_t i = 0; i < batchCount; ++i) batch[i] = events[i];
    count = 0;
    ReleaseSRWLockExclusive(&lock);
    for (size_t i = 0; i < batchCount; ++i) {
        const auto& event = batch[i];
        log("profile-open requestedSteamID=%llu capturedTick=%llu frames=%u (request, not proof of display or current opponent)",
            static_cast<unsigned long long>(event.id), static_cast<unsigned long long>(event.tick), event.frameCount);
        for (USHORT frame = 0; frame < event.frameCount; ++frame) {
            MEMORY_BASIC_INFORMATION info{};
            wchar_t path[MAX_PATH]{};
            const auto address = reinterpret_cast<uintptr_t>(event.frames[frame]);
            if (VirtualQuery(event.frames[frame], &info, sizeof(info)) && info.Type == MEM_IMAGE &&
                GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), path, MAX_PATH)) {
                log("profile-stack frame=%u owner=%ls RVA=0x%llX", frame, path,
                    static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(info.AllocationBase)));
            } else log("profile-stack frame=%u VA=%p", frame, event.frames[frame]);
        }
    }
    if (heartbeat++ % 10 == 0) log("profile-trace heartbeat calls=%llu dropped=%llu installed=%d pinned=%d",
        static_cast<unsigned long long>(calls.load()), static_cast<unsigned long long>(dropped.load()), installed, IsPinned());
}

#ifdef T8_PEER_TRACE_TEST
namespace {
    void* expectedSelf = nullptr;
    const void* expectedCallback = nullptr;
    volatile unsigned fakeCalls = 0;
    __declspec(noinline) void* __fastcall FakeOpen(void* self, uint64_t id, const void* callback) {
        assert(self == expectedSelf && callback == expectedCallback && id == 0x0110000101234567ULL);
        fakeCalls = fakeCalls + 1;
        return self;
    }
}
void TestDetourForwarding() {
    int self = 0, callback = 0;
    expectedSelf = &self;
    expectedCallback = &callback;
    assert(MH_Initialize() == MH_OK);
    auto target = reinterpret_cast<void*>(&FakeOpen);
    assert(MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original)) == MH_OK);
    assert(MH_EnableHook(target) == MH_OK);
    OpenProfile volatile invoke = &FakeOpen;
    for (int i = 0; i < 20; ++i)
        assert(invoke(&self, 0x0110000101234567ULL, &callback) == &self);
    assert(MH_DisableHook(target) == MH_OK);
    assert(MH_RemoveHook(target) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    assert(fakeCalls == 20 && calls.load() == 20 && count == 16 && dropped.load() == 4);
    for (const auto& event : events) assert(event.id == 0x0110000101234567ULL && event.frameCount > 0);
    original = nullptr;
}
#endif
}
