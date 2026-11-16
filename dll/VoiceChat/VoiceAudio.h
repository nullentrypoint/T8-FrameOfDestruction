#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#pragma comment(lib, "winmm.lib")
namespace console { void AddLog(const std::string&); void AddLogError(const std::string&); }

namespace VoiceChat {
struct Microphone { UINT id; std::string name; };
inline std::atomic<bool> voiceEnabled{true}, microphoneMuted{false};
inline std::atomic<int> microphoneGain{100};
inline std::atomic<bool> microphoneTest{false};
inline std::atomic<int> microphoneHotkey{0};
inline std::atomic<float> microphoneLevel{0}, opponentLevel{0};
inline std::atomic<ULONGLONG> localSpokeAt{0}, remoteSpokeAt{0}, remoteLevelAt{0}, microphoneLevelAt{0};
inline std::atomic<bool> captureActive{false};
inline std::atomic<int> remoteMicState{-1}; // -1 unknown/old client, 0 active, 1 muted
inline std::atomic<ULONGLONG> remoteMicStateAt{0};
inline float PcmLevel(const uint8_t* pcm) {
    int peak = 0;
    for (size_t i = 0; i < 320; ++i) {
        short sample; memcpy(&sample, pcm + i * 2, 2);
        peak = std::max(peak, std::abs(int(sample)));
    }
    return peak / 32768.0f;
}
inline short ApplyMicrophoneGain(short sample, int gain) {
    return static_cast<short>(std::clamp(int(sample) * std::clamp(gain, 0, 400) / 100, -32768, 32767));
}
inline std::atomic<UINT> microphoneDevice{WAVE_MAPPER};
inline std::mutex voiceMutex;
inline std::string voiceStatus = "Waiting for Steam transport", audioError;
inline std::vector<Microphone> microphones;
inline std::string preferredMicrophone; // Empty selects the system default.
inline std::atomic<bool> voiceSettingsDirty{false};
// Caller holds voiceMutex. Resolve names again after enumeration because device
// indices may change across restarts or USB reconnects.
inline void ResolveMicrophone() {
    UINT selected = WAVE_MAPPER;
    if (!preferredMicrophone.empty()) {
        const auto found = std::find_if(microphones.begin(), microphones.end(), [](const Microphone& mic) {
            return mic.id != WAVE_MAPPER && mic.name == preferredMicrophone;
        });
        if (found != microphones.end()) selected = found->id;
        else { preferredMicrophone.clear(); voiceSettingsDirty = true; }
    }
    microphoneDevice = selected;
}
inline void SetVoiceStatus(const std::string& value) {
    std::lock_guard lock(voiceMutex); voiceStatus = value;
}

// Fixed wire format: 16 kHz, signed little-endian PCM16 mono, 20 ms.
// Polling buffers avoid callbacks into an unloaded DLL. All handles belong to
// the diagnostic worker; render-thread controls are atomic.
class VoiceAudio {
    struct Buffer { WAVEHDR header{}; std::array<short, 320> samples{}; };
    HWAVEIN input = nullptr;
    HWAVEOUT output = nullptr;
    std::array<Buffer, 6> capture{}, playback{};
    UINT device = WAVE_MAPPER;
    size_t captureIndex = 0;
    bool wasTest = false;
    int receiveVolume = 100;
    bool receiveMuted = false;
    ULONGLONG retryAt = 0, enumerateAt = 0;
    static WAVEFORMATEX Format() { return {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0}; }
    void Error(const char* text) {
        {
            std::lock_guard lock(voiceMutex);
            if (audioError == text) return;
            audioError = text;
        }
        if (*text) console::AddLogError(std::string("[Voice audio] ") + text);
        else console::AddLog("[Voice audio] Microphone capture ready; previous error cleared");
    }
public:
    ~VoiceAudio() { Stop(); }
    void Stop() {
        captureActive = false;
        microphoneLevel = 0;
        localSpokeAt = 0; microphoneLevelAt = 0;
        if (input) {
            waveInReset(input);
            for (auto& b : capture) if (b.header.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(input, &b.header, sizeof(WAVEHDR));
            waveInClose(input); input = nullptr;
        }
        captureIndex = 0;
        if (output) {
            waveOutReset(output);
            for (auto& b : playback) if (b.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &b.header, sizeof(WAVEHDR));
            waveOutClose(output); output = nullptr;
        }
    }
    void Enumerate() {
        if (GetTickCount64() < enumerateAt) return;
        enumerateAt = GetTickCount64() + 3000;
        std::vector<Microphone> list{{WAVE_MAPPER, "System default"}};
        for (UINT i = 0; i < waveInGetNumDevs(); ++i) {
            WAVEINCAPSW caps{};
            if (waveInGetDevCapsW(i, &caps, sizeof(caps))) continue;
            char name[256]{};
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, sizeof(name), nullptr, nullptr);
            list.push_back({i, name});
        }
        std::lock_guard lock(voiceMutex);
        microphones = std::move(list);
        ResolveMicrophone();
    }
    void Update(bool connected, bool peerMuted = false) {
        Enumerate();
        const bool test = microphoneTest.load();
        if (test != wasTest) { Stop(); wasTest = test; }
        const UINT wanted = microphoneDevice.load();
        if (!test && (!connected || !voiceEnabled)) { Stop(); return; }
        if (wanted != device) { Stop(); device = wanted; retryAt = 0; }
        if (GetTickCount64() < retryAt) return;
        auto format = Format();
        if (!output) {
            if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)) {
                output = nullptr; Error("Cannot open playback device"); retryAt = GetTickCount64() + 3000; return;
            }
            for (auto& b : playback) {
                b.header = {}; b.header.lpData = reinterpret_cast<char*>(b.samples.data()); b.header.dwBufferLength = 640;
                if (waveOutPrepareHeader(output, &b.header, sizeof(WAVEHDR))) { Stop(); Error("Playback buffer error"); retryAt = GetTickCount64() + 3000; return; }
            }
        }
        if ((microphoneMuted || peerMuted) && !test) {
            captureActive = false;
            microphoneLevel = 0;
            if (input) { // Reset discards captured speech immediately on mute.
                waveInReset(input);
                for (auto& b : capture) waveInUnprepareHeader(input, &b.header, sizeof(WAVEHDR));
                waveInClose(input); input = nullptr;
            }
            return;
        }
        if (!input) {
            captureIndex = 0;
            if (waveInOpen(&input, device, &format, 0, 0, CALLBACK_NULL)) {
                input = nullptr; Error("Cannot open microphone (check device and Windows permissions)"); retryAt = GetTickCount64() + 3000; return;
            }
            for (auto& b : capture) {
                b.header = {}; b.header.lpData = reinterpret_cast<char*>(b.samples.data()); b.header.dwBufferLength = 640;
                if (waveInPrepareHeader(input, &b.header, sizeof(WAVEHDR)) || waveInAddBuffer(input, &b.header, sizeof(WAVEHDR))) {
                    Stop(); Error("Microphone buffer error"); retryAt = GetTickCount64() + 3000; return;
                }
            }
            if (waveInStart(input)) { Stop(); Error("Cannot start microphone"); retryAt = GetTickCount64() + 3000; return; }
            Error("");
        }
        captureActive = input != nullptr;
    }
    void SetReceive(int volume, bool muted) {
        if (receiveVolume != volume || receiveMuted != muted) {
            if (output && !microphoneTest) waveOutReset(output);
            receiveVolume = volume; receiveMuted = muted;
        }
    }
    void FlushPlayback() {
        if (output && !microphoneTest) waveOutReset(output);
    }
    template<class Send> void Capture(Send send) {
        const bool test = microphoneTest.load();
        if (!input || (!test && (microphoneMuted || !voiceEnabled))) return;
        // Driver completes these cyclic buffers in submission order.
        for (size_t count = 0; count < capture.size(); ++count) {
            auto& b = capture[captureIndex];
            if (!(b.header.dwFlags & WHDR_DONE)) break;
            if (b.header.dwBytesRecorded == 640) {
                std::array<short, 320> amplified{};
                const int gain = microphoneGain.load();
                for (size_t i = 0; i < amplified.size(); ++i) amplified[i] = ApplyMicrophoneGain(b.samples[i], gain);
                const float level = PcmLevel(reinterpret_cast<const uint8_t*>(amplified.data()));
                microphoneLevel = level;
                microphoneLevelAt = GetTickCount64();
                if (level > .025f) localSpokeAt = GetTickCount64();
                if (test) Play(reinterpret_cast<const uint8_t*>(amplified.data()), true);
                else send(amplified.data());
            }
            b.header.dwBytesRecorded = 0;
            waveInAddBuffer(input, &b.header, sizeof(WAVEHDR));
            captureIndex = (captureIndex + 1) % capture.size();
        }
    }
    void Play(const uint8_t* pcm, bool monitor = false) {
        if (!output || (!monitor && (!voiceEnabled || receiveMuted || microphoneTest))) return;
        for (auto& b : playback) if (!(b.header.dwFlags & WHDR_INQUEUE)) {
            const int volume = monitor ? 100 : std::clamp(receiveVolume, 0, 100);
            for (size_t i = 0; i < b.samples.size(); ++i) {
                short sample; memcpy(&sample, pcm + i * 2, 2);
                b.samples[i] = static_cast<short>(int(sample) * volume / 100);
            }
            waveOutWrite(output, &b.header, sizeof(WAVEHDR));
            return; // Drop excess packets instead of accumulating latency.
        }
    }
    void PlayMixed(const uint8_t* pcm) {
        if (!output || microphoneTest || !voiceEnabled) return;
        // Keep two blocks of scheduling headroom. A single 20 ms buffer underruns
        // with Windows' ~15.6 ms polling granularity even on a lossless network.
        const bool queued = std::any_of(playback.begin(), playback.end(),
            [](const auto& b) { return (b.header.dwFlags & WHDR_INQUEUE) != 0; });
        if (!queued) {
            std::array<uint8_t, 640> silence{};
            Play(silence.data()); Play(silence.data());
        }
        Play(pcm);
    }
};
}
