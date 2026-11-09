// Standalone lifecycle check; does not load the game or attach a debugger.
#include "stdafx.h"
#include "VoiceChat/SteamDiagnostics.h"
#include "VoiceChat/PeerCounters.h"
#include "VoiceChat/VoiceAudio.h"
#include "VoiceChat/PeerPreferences.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <cassert>
#include <thread>

static void TestPeerCounters() {
    SteamDiagnostics::PeerCounters counters;
    counters.Record(42, 1, 0, 0, 10, true);
    counters.Record(42, 1, 0, 0, 20, false);
    counters.Record(42, 1, 765, 0, 30, true);
    uint64_t lost = 0;
    auto snapshot = counters.Snapshot(lost);
    assert(snapshot[0].calls == 2 && snapshot[0].accepted == 1 && snapshot[0].bytes == 30);
    assert(snapshot[1].calls == 1 && snapshot[1].channel == 765 && !lost);
    for (uint64_t peer = 100; peer < 163; ++peer) counters.Record(peer, 1, 0, 0, 1, true);
    snapshot = counters.Snapshot(lost);
    assert(lost == 1 && snapshot.back().peer == 161);

    SteamDiagnostics::PeerCounters concurrent;
    std::array<std::thread, 4> threads;
    for (auto& thread : threads) thread = std::thread([&] {
        for (int i = 0; i < 10000; ++i) concurrent.Record(42, 1, 0, 0, 10, true);
    });
    for (auto& thread : threads) thread.join();
    snapshot = concurrent.Snapshot(lost);
    assert(snapshot[0].calls + lost == 40000);
    assert(snapshot[0].accepted == snapshot[0].calls && snapshot[0].bytes == snapshot[0].calls * 10);
}

namespace globals { HMODULE mainModule = nullptr; }
// Rendering is not linked into this headless harness.
namespace menu { void QueueNotification(std::string, std::string, double) {} }
namespace console { void AddLog(const std::string&) {} void AddLogError(const std::string&) {} }
void TestPlayerIdentityReader();
void TestIdentitySession();
void TestSteamProbeProtocol();
#ifdef T8_PEER_TRACE_TEST
namespace SteamDiagnostics::PeerTrace { void TestDetourForwarding(); }
namespace SteamDiagnostics::ProfileTrace { void TestDetourForwarding(); }
namespace VoiceChat { void TestSteamProbeCandidates(); }
#endif

int main() {
    // Amplification must saturate rather than wrap/invert loud PCM samples.
    assert(VoiceChat::ApplyMicrophoneGain(12000, 100) == 12000);
    assert(VoiceChat::ApplyMicrophoneGain(-12000, 200) == -24000);
    assert(VoiceChat::ApplyMicrophoneGain(20000, 400) == 32767);
    assert(VoiceChat::ApplyMicrophoneGain(-20000, 400) == -32768);
    assert(VoiceChat::ApplyMicrophoneGain(-32768, 100) == -32768);
    assert(VoiceChat::ApplyMicrophoneGain(32767, 0) == 0);
#ifdef T8_PEER_TRACE_TEST
    SteamDiagnostics::PeerTrace::TestDetourForwarding();
    SteamDiagnostics::ProfileTrace::TestDetourForwarding();
    VoiceChat::TestSteamProbeCandidates();
#endif
    TestPeerCounters();
    TestPlayerIdentityReader();
    TestIdentitySession();
    TestSteamProbeProtocol();
    globals::mainModule = GetModuleHandleW(nullptr);
    wchar_t path[MAX_PATH]{};
    assert(GetModuleFileNameW(globals::mainModule, path, MAX_PATH));
    const auto root = std::filesystem::path(path).parent_path();
    const auto prefsRoot = root / ("voice_preferences_test_" + std::to_string(GetCurrentProcessId()));
    assert(!std::filesystem::exists(prefsRoot));
    std::filesystem::create_directory(prefsRoot);
    {
        VoiceChat::PeerPreferences prefs;
        prefs.Configure(prefsRoot / "user_settings.ini");
        // Fixed synthetic Steam IDs, unrelated to captured players.
        assert(prefs.Get(76561202131974853ULL).volume == 100);
        assert(prefs.Set(76561202131974853ULL, {37, true}));
        assert(prefs.Set(76561202059183741ULL, {82, false}));
        VoiceChat::PeerPreferences reopened;
        reopened.Configure(prefsRoot / "user_settings.ini");
        const auto first = reopened.Get(76561202131974853ULL);
        const auto second = reopened.Get(76561202059183741ULL);
        assert(first.volume == 37 && first.is_mute);
        assert(second.volume == 82 && !second.is_mute);
        assert(!std::filesystem::exists(prefsRoot / "user_settings.ini"));
        assert(WritePrivateProfileStringW(L"76561202131974853", L"volume", L"bad", (prefsRoot / "voice_peers.ini").c_str()));
        VoiceChat::PeerPreferences invalid;
        invalid.Configure(prefsRoot / "user_settings.ini");
        assert(invalid.Get(76561202131974853ULL).volume == 100);
        assert(invalid.Get(76561202131974853ULL).is_mute);
        invalid.Configure(prefsRoot / "missing" / "user_settings.ini");
        assert(!invalid.Set(76561202131974853ULL, {50, false}));
        assert(!invalid.Error().empty());
    }
    std::filesystem::remove_all(prefsRoot);
    const auto marker = root / L"steam_diagnostics.enabled";
    const auto log = root / L"steam_diagnostics.log";
    // Use a fresh test output directory to preserve any existing diagnostics.
    assert(!std::filesystem::exists(marker) && !std::filesystem::exists(log));
    SteamDiagnostics::Start();
    SteamDiagnostics::Stop();
    assert(!std::filesystem::exists(log));
    { std::ofstream enable(marker); assert(enable.good()); }
    DWORD initialHandles = 0, finalHandles = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &initialHandles));
    for (int i = 0; i < 10; ++i) {
        SteamDiagnostics::Start();
        SteamDiagnostics::Start();
        // The diagnostic log must be readable while its writer is running.
        HANDLE liveLog = CreateFileW(log.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(liveLog != INVALID_HANDLE_VALUE);
        CloseHandle(liveLog);
        SteamDiagnostics::Stop();
        SteamDiagnostics::Stop();
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &finalHandles));
    assert(finalHandles == initialHandles);
    std::ifstream input(log);
    std::string line;
    int starts = 0, stops = 0, skipped = 0;
    while (std::getline(input, line)) {
        if (line.find("diagnostics started") != std::string::npos) ++starts;
        if (line.find("diagnostics stopped") != std::string::npos) ++stops;
        if (line.find("validation=skipped") != std::string::npos) ++skipped;
    }
    assert(starts == 10 && stops == 10 && skipped == 30);
    input.close();
    std::filesystem::remove(marker);
    std::filesystem::remove(log);
    puts("Steam diagnostics lifecycle: PASS (opt-in, unknown build, repeat start/stop, handles)");
}
