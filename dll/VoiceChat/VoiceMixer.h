#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace VoiceChat {
using VoiceBlock = std::array<int16_t, 320>;
inline float ProximityGain(double distance, double radius) {
    if (!std::isfinite(distance) || !std::isfinite(radius) || distance < 0 || radius <= 0 || distance >= radius) return 0;
    if (distance <= radius * .25) return 1;
    const double t = (distance - radius * .25) / (radius * .75);
    return static_cast<float>(1 - t * t * (3 - 2 * t));
}
inline bool NewSequence(uint32_t value, uint32_t previous) {
    return value != previous && uint32_t(value - previous) < 0x80000000u;
}
// Duplicate/replay window, allowing bounded reordering and uint32 wrap.
class VoiceSequenceWindow {
    uint32_t highest = 0, bits = 0;
public:
    bool Accept(uint32_t sequence) {
        if (!bits) { highest = sequence; bits = 1; return true; }
        if (NewSequence(sequence, highest)) {
            const auto delta = sequence - highest;
            bits = delta >= 32 ? 1 : (bits << delta) | 1;
            highest = sequence; return true;
        }
        const auto age = highest - sequence;
        if (age >= 32 || (bits & (uint32_t{1} << age))) return false;
        bits |= uint32_t{1} << age;
        return true;
    }
};
class VoiceJitterBuffer {
    struct Packet { uint32_t sequence; uint64_t at; VoiceBlock pcm; };
    std::vector<Packet> packets;
    uint32_t expected = 0;
    uint64_t startAt = 0, lastAt = 0;
    bool started = false, playing = false;
public:
    void Clear() { packets.clear(); started = playing = false; startAt = lastAt = 0; }
    void Push(uint32_t sequence, const VoiceBlock& pcm, uint64_t now) {
        if (started && now - lastAt > 200) Clear();
        if (!started) { expected = sequence; startAt = now; started = true; }
        if (sequence != expected && !NewSequence(sequence, expected)) {
            if (playing || expected - sequence >= 6) return;
            expected = sequence; // A slightly older packet may arrive during initial buffering.
        }
        if (sequence - expected >= 6) {
            Clear(); expected = sequence; startAt = now; started = true;
        }
        if (std::any_of(packets.begin(), packets.end(), [&](const auto& p) { return p.sequence == sequence; })) return;
        if (packets.size() >= 6) return;
        packets.push_back({sequence, now, pcm}); lastAt = now;
    }
    VoiceBlock Pop(uint64_t now) {
        VoiceBlock result{};
        if (!started || now - startAt < 40) return result; // Two 20 ms blocks of initial jitter tolerance.
        if (now - lastAt > 200) { Clear(); return result; }
        playing = true;
        for (const auto& packet : packets)
            if (packet.sequence == expected && now - packet.at <= 200) { result = packet.pcm; break; }
        ++expected;
        std::erase_if(packets, [&](const auto& p) {
            return now - p.at > 200 || (p.sequence != expected && !NewSequence(p.sequence, expected));
        });
        return result;
    }
};
class VoiceMix {
    std::array<double, 320> sum{};
public:
    void Add(const VoiceBlock& pcm, float gain) {
        if (!std::isfinite(gain) || gain <= 0) return;
        gain = std::clamp(gain, 0.0f, 1.0f);
        for (size_t i = 0; i < sum.size(); ++i) sum[i] += pcm[i] * gain;
    }
    VoiceBlock Finish() const {
        VoiceBlock result;
        for (size_t i = 0; i < sum.size(); ++i)
            result[i] = static_cast<int16_t>(std::clamp(sum[i], -32768.0, 32767.0));
        return result;
    }
};
}
