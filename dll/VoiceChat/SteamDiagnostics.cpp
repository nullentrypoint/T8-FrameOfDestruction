#include "../stdafx.h"
#include "SteamDiagnostics.h"
#include "SteamPeerTrace.h"
#include "SteamProfileTrace.h"
#include "PlayerIdentityReader.h"
#include "IdentityTransitions.h"
#include "IdentitySession.h"
#include "SteamChannelProbe.h"
#include "MemoryLayout.h"
#include "LoungeLayout.h"
#include "VoicePresentation.h"
#include <TlHelp32.h>
#include <filesystem>
#include <array>
#include <share.h>
#include <mutex>
#ifdef T8_PEER_TRACE_TEST
#include <cassert>
#endif

namespace SteamDiagnostics {
namespace {
    HANDLE stopEvent = nullptr;
    HANDLE worker = nullptr;
    FILE* logFile = nullptr;
    bool traceRequested = false;
    bool profileRequested = false;
    bool channelProbeRequested = false;
    VoiceChat::MemoryLayout memoryLayout;
    std::string memoryLayoutError;
    VoiceChat::LoungeLayout loungeLayout;
    VoiceChat::CustomLobbyLayout customLobbyLayout;
    std::string customLobbyLayoutError;
    VoiceChat::LoungeCameraLayout loungeCamera;
    std::string loungeLayoutError;
    std::mutex stopMutex;
    std::atomic<bool> steamShuttingDown{false};
    using SteamShutdown = void(__cdecl*)();
    SteamShutdown originalShutdown = nullptr;
    void* shutdownTarget = nullptr;
    HMODULE shutdownModule = nullptr;
    void StopWorker(bool steamShutdown = false);

    void __cdecl SteamShutdownHook() {
        steamShuttingDown.store(true);
        // Still before Steam shutdown: CloseChannelWithUser and other worker
        // cleanup may use the SDK while this thread waits for the worker to exit.
        StopWorker(true);
        originalShutdown();
    }

    bool InstallShutdownHook(void* target) {
        if (shutdownTarget) return shutdownTarget == target && !steamShuttingDown.load();
        if (MH_CreateHook(target, reinterpret_cast<void*>(&SteamShutdownHook),
            reinterpret_cast<void**>(&originalShutdown)) != MH_OK) return false;
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            originalShutdown = nullptr;
            return false;
        }
        shutdownTarget = target;
        return true;
    }

    bool Read(uintptr_t address, void* output, size_t size) {
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            output, size, &read) && read == size;
    }

    template<typename T> bool Read(uintptr_t address, T& value) {
        return Read(address, &value, sizeof(value));
    }

    void Log(const char* format, ...) {
        fprintf(logFile, "[%llu] ", GetTickCount64());
        va_list args;
        va_start(args, format);
        va_list consoleArgs;
        va_copy(consoleArgs, args);
        char message[4096]{};
        vsnprintf_s(message, sizeof(message), _TRUNCATE, format, consoleArgs);
        va_end(consoleArgs);
        vfprintf(logFile, format, args);
        va_end(args);
        fputc('\n', logFile);
        fflush(logFile);
        const std::string_view text(message);
        if (text.starts_with("steam-probe ") || text.starts_with("voice-audio ") ||
            text.starts_with("voice-profile ") || text.starts_with("identity-session ") ||
            text.starts_with("player-record ") || text.starts_with("Steam diagnostics started") ||
            text.starts_with("passive Steam diagnostics stopped")) {
            const bool error = text.find("send failed") != text.npos || text.find("timeout") != text.npos ||
                text.find("binding=") != text.npos || text.find("no-ACK") != text.npos ||
                (text.starts_with("steam-probe send-result=") && !text.starts_with("steam-probe send-result=1 "));
            if (error) console::AddLogError("[Voice] " + std::string(text));
            else console::AddLog("[Voice] " + std::string(text));
        }
    }

    void Owner(const char* label, uintptr_t address) {
        MEMORY_BASIC_INFORMATION info{};
        wchar_t path[MAX_PATH]{};
        if (address && VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) &&
            info.Type == MEM_IMAGE && GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), path, MAX_PATH)) {
            Log("%s=%p owner=%ls RVA=0x%llX", label, reinterpret_cast<void*>(address), path,
                static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(info.AllocationBase)));
        } else {
            Log("%s=%p owner=unknown", label, reinterpret_cast<void*>(address));
        }
    }

    struct Context {
        const char* name;
        uintptr_t contextRva;
        uintptr_t initRva;
        uintptr_t lastGeneration = ~uintptr_t(0);
        uintptr_t lastPointer = ~uintptr_t(0);
        bool valid = false;
    };

    // Research candidates from the inspected build. Validate their initializer,
    // interface string, and import target before reading the cached context.
    bool Validate(uintptr_t base, const Context& context) {
        uintptr_t initializer = 0;
        if (!Read(base + context.contextRva, initializer) || initializer != base + context.initRva)
            return false;
        std::array<unsigned char, 33> code{};
        if (!Read(initializer, code.data(), code.size())) return false;
        const unsigned char prefix[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xFF, 0x15};
        if (memcmp(code.data(), prefix, sizeof(prefix)) != 0 ||
            code[15] != 0x8B || code[16] != 0xC8 ||
            code[17] != 0x48 || code[18] != 0x8D || code[19] != 0x15 ||
            code[24] != 0xFF || code[25] != 0x15 ||
            code[30] != 0x48 || code[31] != 0x89 || code[32] != 0x03) return false;
        int32_t displacement = 0;
        memcpy(&displacement, code.data() + 20, sizeof(displacement));
        char name[64]{};
        const auto length = strlen(context.name) + 1;
        if (!Read(initializer + 24 + displacement, name, length) || strcmp(name, context.name) != 0)
            return false;
        memcpy(&displacement, code.data() + 26, sizeof(displacement));
        uintptr_t target = 0;
        if (!Read(initializer + 30 + displacement, target) || !target) return false;
        Owner("FindOrCreateUserInterface target", target);
        return true;
    }

    void Modules() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snapshot == INVALID_HANDLE_VALUE) {
            Log("module snapshot failed: %lu", GetLastError());
            return;
        }
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module)) do {
            // Log every module so renamed SDK wrappers remain visible.
            Log("module base=%p size=%lu path=%ls", module.modBaseAddr, module.modBaseSize, module.szExePath);
        } while (Module32NextW(snapshot, &module));
        CloseHandle(snapshot);
    }

    DWORD WINAPI Run(void*) {
        Log("Steam diagnostics started pid=%lu peer-trace-requested=%d profile-trace-requested=%d",
            GetCurrentProcessId(), traceRequested, profileRequested);
        Log("steam-channel-probe requested=%d; audio=PCM16/16000/mono", channelProbeRequested);
        Modules();
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!memoryLayoutError.empty()) Log("steam-probe binding=%s", memoryLayoutError.c_str());
        if (profileRequested) {
            const bool installed = ProfileTrace::Install(base, memoryLayout);
            Log("profile-trace install=%s pinned=%d; pinned tracing requires game exit to unload",
                installed ? "success" : "failed", ProfileTrace::IsPinned());
        }
        Context contexts[] = {
            {"SteamNetworking006", memoryLayout.networkingContext, memoryLayout.networkingInitializer},
            {"SteamMatchMaking009", memoryLayout.matchmakingContext, memoryLayout.matchmakingInitializer},
            {"SteamUser021", memoryLayout.userContext, memoryLayout.userInitializer},
        };
        for (auto& context : contexts) {
            context.valid = Validate(base, context);
            Log("%s contextRVA=0x%llX validation=%s", context.name,
                static_cast<unsigned long long>(context.contextRva), context.valid ? "matched" : "skipped");
        }
        bool traceAttempted = false;
        VoiceChat::PlayerIdentityReader identities(base, memoryLayout.identity);
        VoiceChat::IdentitySnapshot previousIdentities;
        bool haveIdentities = false;
        VoiceChat::IdentityTransitions identityTransitions;
        VoiceChat::IdentitySession identitySession;
        VoiceChat::SteamChannelProbe channelProbe(Log, memoryLayout.userInitializer);
        VoiceChat::LoungeReader loungeReader(base, loungeLayout,
            [](void*, uintptr_t address, void* output, size_t size) { return Read(address, output, size); }, nullptr);
        VoiceChat::CustomLobbyReader customReader(base, customLobbyLayout,
            [](void*, uintptr_t address, void* output, size_t size) { return Read(address, output, size); }, nullptr);
        if (!loungeLayoutError.empty()) Log("lounge-reader disabled: %s", loungeLayoutError.c_str());
        if (!customLobbyLayoutError.empty()) Log("custom-room-reader disabled: %s", customLobbyLayoutError.c_str());
        VoiceChat::IdentitySnapshot identitySnapshot;
        ULONGLONG nextIdentityRead = 0;
        ULONGLONG nextLoungeRead = 0;
        VoiceChat::LoungeSnapshot latestLounge;
        do {
            if (GetTickCount64() >= nextIdentityRead) {
                identitySnapshot = identities.Read();
                nextIdentityRead = GetTickCount64() + 100;
            }
            for (const auto& event : identitySession.Update(identitySnapshot)) {
                const char* type = "unknown";
                switch (event.type) {
                case VoiceChat::IdentitySessionEventType::Started: type = "started"; break;
                case VoiceChat::IdentitySessionEventType::Ended: type = "ended"; break;
                case VoiceChat::IdentitySessionEventType::Suspended: type = "suspended"; break;
                case VoiceChat::IdentitySessionEventType::Resumed: type = "resumed"; break;
                }
                Log("identity-session event=%s generation=%llu reason=%s local=%s remote=%s platformA=%s platformB=%s",
                    type, static_cast<unsigned long long>(event.session.generation), event.reason,
                    event.session.localIds[1].c_str(), event.session.remoteIds[1].c_str(),
                    event.session.remoteIds[2].c_str(), event.session.remoteIds[3].c_str());
            }
            if (latestLounge.status == VoiceChat::LoungeReadStatus::Ok &&
                !(latestLounge.customRoom ? customReader.StillCurrent(latestLounge) : loungeReader.StillCurrent(latestLounge)))
                nextLoungeRead = 0;
            if (GetTickCount64() >= nextLoungeRead) {
                latestLounge = loungeLayoutError.empty() ? loungeReader.Read() :
                    VoiceChat::LoungeSnapshot{VoiceChat::LoungeReadStatus::UnsupportedBuild};
                if (latestLounge.status != VoiceChat::LoungeReadStatus::Ok && customLobbyLayoutError.empty()) {
                    const auto custom = customReader.Read();
                    if (custom.status != VoiceChat::LoungeReadStatus::NotInLounge &&
                        custom.status != VoiceChat::LoungeReadStatus::UnsupportedBuild) latestLounge = custom;
                }
                channelProbe.UpdateLounge(latestLounge, loungeCamera);
                nextLoungeRead = GetTickCount64() + 200;
            }
            if (channelProbeRequested) channelProbe.Update(identitySnapshot, identitySession);
            for (const auto& event : identityTransitions.Update(identitySnapshot)) {
                Log("player-record %s %s", event.appeared ? "appeared" : "disappeared",
                    VoiceChat::IdentityNotificationDetail(event.player).c_str());
            }
            if (!haveIdentities || identitySnapshot != previousIdentities) {
                Log("player-identities status=%s root=%p count=%zu active-match=unverified",
                    VoiceChat::IdentityStatusName(identitySnapshot.status), reinterpret_cast<void*>(identitySnapshot.root),
                    identitySnapshot.players.size());
                for (size_t i = 0; i < identitySnapshot.players.size(); ++i) {
                    const auto& player = identitySnapshot.players[i];
                    Log("player-identity index=%zu address=%p words=%u,%u,%u,%u id60=%s tekken=%s platformA=%s platformB=%s",
                        i, reinterpret_cast<void*>(player.address), player.rawWords[0], player.rawWords[1],
                        player.rawWords[2], player.rawWords[3], player.ids[0].c_str(), player.ids[1].c_str(),
                        player.ids[2].c_str(), player.ids[3].c_str());
                }
                previousIdentities = identitySnapshot;
                haveIdentities = true;
            }
            for (auto& context : contexts) {
                if (!context.valid) continue;
                std::array<uintptr_t, 3> values{};
                if (!Read(base + context.contextRva, values.data(), sizeof(values))) {
                    Log("%s context unreadable; disabling sample", context.name);
                    context.valid = false;
                    continue;
                }
                if (traceRequested && !traceAttempted && context.contextRva == memoryLayout.networkingContext && values[2]) {
                    traceAttempted = true;
                    const bool installed = PeerTrace::Install(base, values[2], memoryLayout);
                    Log("peer-trace install=%s pinned=%d; pinned tracing requires game exit to unload",
                        installed ? "success" : "failed", PeerTrace::IsPinned());
                }
                if (values[1] == context.lastGeneration && values[2] == context.lastPointer) continue;
                context.lastGeneration = values[1];
                context.lastPointer = values[2];
                Log("%s generation=%llu interface=%p", context.name,
                    static_cast<unsigned long long>(values[1]), reinterpret_cast<void*>(values[2]));
                uintptr_t vtable = 0;
                if (values[2] && Read(values[2], vtable)) {
                    Owner("vtable", vtable);
                    uintptr_t method = 0;
                    if (Read(vtable, method)) Owner("method[0]", method);
                }
            }
            if (traceRequested) PeerTrace::Flush(Log);
            if (profileRequested) ProfileTrace::Flush(Log);
        } while (WaitForSingleObject(stopEvent, channelProbeRequested ? 10 : 1000) == WAIT_TIMEOUT);
        channelProbe.Stop();
        { std::lock_guard lock(VoiceChat::presentationMutex); VoiceChat::loungeView = {}; }
        Log("passive Steam diagnostics stopped");
        return 0;
    }
}

void Start() {
    if (worker) return;
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(globals::mainModule, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    const auto directory = std::filesystem::path(modulePath).parent_path();
    if (GetFileAttributesW((directory / L"steam_diagnostics.enabled").c_str()) == INVALID_FILE_ATTRIBUTES) return;
    traceRequested = GetFileAttributesW((directory / L"steam_peer_trace.enabled").c_str()) != INVALID_FILE_ATTRIBUTES;
    profileRequested = GetFileAttributesW((directory / L"steam_profile_trace.enabled").c_str()) != INVALID_FILE_ATTRIBUTES;
    channelProbeRequested = GetFileAttributesW((directory / L"steam_channel_probe.enabled").c_str()) != INVALID_FILE_ATTRIBUTES;
    logFile = _wfsopen((directory / L"steam_diagnostics.log").c_str(), L"a", _SH_DENYNO);
    if (!logFile) return;
    VoiceChat::LoadMemoryLayout(directory / L"T8FrameOfDestruction" / L"config" / L"memory_address.ini",
        memoryLayout, memoryLayoutError);
    const auto loungePath = directory / L"T8FrameOfDestruction" / L"config" / L"memory_address.ini";
    VoiceChat::LoadLoungeLayout(loungePath, loungeLayout, loungeLayoutError);
    VoiceChat::LoadCustomLobbyLayout(loungePath, customLobbyLayout, customLobbyLayoutError);
    loungeCamera = VoiceChat::LoadLoungeCameraLayout(loungePath);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent) worker = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (!worker) {
        Log("diagnostic worker creation failed: %lu", GetLastError());
        if (stopEvent) CloseHandle(stopEvent);
        stopEvent = nullptr;
        fclose(logFile);
        logFile = nullptr;
    }
}

void Stop() {
    StopWorker();
    // During SDK shutdown the detour/trampoline may still be on the game's
    // stack. Keep them alive until process exit; Uninject checks this flag.
    if (!shutdownTarget || steamShuttingDown.load()) return;
    if (MH_DisableHook(shutdownTarget) != MH_OK) return;
    if (steamShuttingDown.load()) return;
    MH_RemoveHook(shutdownTarget);
    shutdownTarget = nullptr;
    originalShutdown = nullptr;
    if (shutdownModule) FreeLibrary(shutdownModule);
    shutdownModule = nullptr;
}

bool GuardSteamShutdown(HMODULE sdk) {
    if (steamShuttingDown.load()) return false;
    auto target = reinterpret_cast<void*>(GetProcAddress(sdk, "SteamAPI_Shutdown"));
    if (!target) return false;
    if (shutdownTarget) return shutdownTarget == target;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(target), &owner)) return false;
    if (!InstallShutdownHook(target)) { FreeLibrary(owner); return false; }
    shutdownModule = owner;
    return true;
}

bool IsSteamShuttingDown() { return steamShuttingDown.load(); }

namespace {
void StopWorker(bool steamShutdown) {
    // Called outside DllMain, both for explicit unload and before SDK shutdown.
    std::lock_guard lock(stopMutex);
    if (!worker) return;
    if (steamShutdown) Log("steam-probe stopping worker before SteamAPI_Shutdown");
    SetEvent(stopEvent);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CloseHandle(stopEvent);
    worker = stopEvent = nullptr;
    fclose(logFile);
    logFile = nullptr;
}
}

#ifdef T8_PEER_TRACE_TEST
namespace {
    std::atomic<bool> testWorkerFinished{false};
    unsigned testShutdownCalls = 0;
    DWORD WINAPI TestSteamWorker(void*) {
        assert(WaitForSingleObject(stopEvent, INFINITE) == WAIT_OBJECT_0);
        // Represents the last worker cleanup that still needs a live SDK.
        testWorkerFinished.store(true);
        return 0;
    }
    __declspec(noinline) void __cdecl TestSteamShutdown() {
        assert(testWorkerFinished.load());
        assert(!worker && !stopEvent && !logFile);
        ++testShutdownCalls;
    }
}

void TestShutdownOrdering() {
    assert(MH_Initialize() == MH_OK);
    SteamShutdown volatile invoke = &TestSteamShutdown;
    for (bool explicitUnload : {false, true}) {
        testWorkerFinished.store(false);
        testShutdownCalls = 0;
        assert(tmpfile_s(&logFile) == 0 && logFile);
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        assert(stopEvent);
        worker = CreateThread(nullptr, 0, TestSteamWorker, nullptr, 0, nullptr);
        assert(worker);
        assert(InstallShutdownHook(reinterpret_cast<void*>(&TestSteamShutdown)));
        if (explicitUnload) {
            Stop();
            assert(!shutdownTarget && !IsSteamShuttingDown());
        }
        invoke();
        invoke(); // Repeated SDK shutdown must not use the closed worker/log.
        assert(testShutdownCalls == 2);
        assert(IsSteamShuttingDown() == !explicitUnload);
        if (shutdownTarget) {
            // Test has returned from the detour, unlike a concurrent F11 unload.
            assert(MH_DisableHook(shutdownTarget) == MH_OK);
            assert(MH_RemoveHook(shutdownTarget) == MH_OK);
            shutdownTarget = nullptr;
            originalShutdown = nullptr;
        }
        steamShuttingDown.store(false);
    }
    assert(MH_Uninitialize() == MH_OK);
}
#endif
}
