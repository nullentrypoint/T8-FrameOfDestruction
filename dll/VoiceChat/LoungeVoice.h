#pragma once
#include "LoungeReader.h"
#include "LoungeVoiceProtocol.h"
#include "VoiceMixer.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace VoiceChat {
inline constexpr int DefaultLoungeVoiceDistance = -1; // Unlimited, same-room voice.

// All methods run on the existing voice worker. No SDK/device calls here;
// dependency callbacks let tests exercise multiple independent clients.
class LoungeVoice {
public:
    struct Controls { int volume = 100; bool muted = false; };
    struct Peer {
        LoungeProtocol::Handshake handshake;
        VoiceJitterBuffer jitter;
        VoiceSequenceWindow audioSequences;
        double distance = 0;
        float gain = 0, level = 0;
        uint64_t lastAck = 0, nextHello = 0, startedAt = 0, connectedAt = 0;
        uint64_t levelAt = 0, spokeAt = 0, micStateAt = 0, nextMic = 0;
        uint64_t sent = 0, received = 0, failed = 0;
        uint64_t wireReceived = 0, rejected = 0;
        uint64_t helloSent = 0, helloFailed = 0;
        uint32_t sendSequence = 0, sendMicSequence = 0, remoteMicSequence = 0;
        bool haveMicSequence = false;
        int micState = -1, lastSentMic = -1;
        Controls controls;
    };
    using Send = std::function<bool(uint64_t, const LoungeProtocol::Bytes&, bool)>;
    using Close = std::function<void(uint64_t)>;
    using Random = std::function<bool(LoungeProtocol::Nonce&)>;
    using Preferences = std::function<Controls(uint64_t)>;
private:
    Send send;
    Close close;
    Random random;
    Preferences preferences;
    std::unordered_map<uint64_t, Peer> peers;
    uint64_t local = 0, nextMix = 0;
    uintptr_t world = 0;
    std::string room;
    bool flushPlayback = false;
    uint64_t unknownSenders = 0;
    void ClearAudio(Peer& peer) {
        peer.jitter.Clear(); peer.level = 0; peer.levelAt = peer.spokeAt = 0;
        flushPlayback = true;
    }
    void NewEpoch(Peer& peer) {
        ClearAudio(peer); peer.audioSequences = {};
        peer.haveMicSequence = false; peer.micState = -1; peer.micStateAt = 0;
        peer.nextMic = 0; peer.lastSentMic = -1;
    }
    bool Begin(uint64_t id, Peer& peer, uint64_t now) {
        LoungeProtocol::Nonce nonce{};
        if (!random(nonce) || ProbeProtocol::Empty(nonce) || !peer.handshake.Begin(local, id, room, nonce)) return false;
        NewEpoch(peer);
        peer.nextHello = 0; peer.lastAck = 0; peer.startedAt = now; peer.connectedAt = 0;
        peer.sendSequence = peer.sendMicSequence = 0;
        return true;
    }
    void End(uint64_t id, Peer& peer) {
        if (const auto bye = peer.handshake.End()) send(id, *bye, true);
        close(id);
        ClearAudio(peer);
    }
public:
    LoungeVoice(Send s, Close c, Random r, Preferences p)
        : send(std::move(s)), close(std::move(c)), random(std::move(r)), preferences(std::move(p)) {}
    const auto& Peers() const { return peers; }
    uint64_t UnknownSenders() const { return unknownSenders; }
    size_t ConnectedCount() const {
        return static_cast<size_t>(std::count_if(peers.begin(), peers.end(), [](const auto& p) { return p.second.handshake.Ready(); }));
    }
    bool CanSendToAnyone() const {
        return std::any_of(peers.begin(), peers.end(), [](const auto& p) {
            return p.second.handshake.Ready() && p.second.gain > 0 && !p.second.controls.muted;
        });
    }
    bool TakePlaybackReset() { const bool result = flushPlayback; flushPlayback = false; return result; }
    void Reset() {
        for (auto& [id, peer] : peers) End(id, peer);
        peers.clear(); world = 0; local = 0; room.clear(); nextMix = 0;
    }
    void Sync(uint64_t localId, const LoungeSnapshot& snapshot, uint64_t now, int radius, bool enabled) {
        if (snapshot.customRoom) {
            radius = 1; // Room-wide voice is independent of the Fight Lounge distance setting.
            if (std::none_of(snapshot.participants.begin(), snapshot.participants.end(),
                [&](const auto& member) { return member.steamIdCandidate == localId; })) { Reset(); return; }
        }
        const bool unlimited = radius == -1;
        if (!enabled || radius == 0 || radius < -1 || radius > 10000 || !localId ||
            snapshot.status != LoungeReadStatus::Ok || !snapshot.world ||
            snapshot.participants.size() > 128 || !LoungeProtocol::ValidRoom(snapshot.roomKey)) { Reset(); return; }
        if (local != localId || world != snapshot.world || room != snapshot.roomKey) {
            Reset(); local = localId; world = snapshot.world; room = snapshot.roomKey;
        }
        std::unordered_map<uint64_t, double> eligible;
        for (const auto& member : snapshot.participants) {
            const auto id = member.steamIdCandidate;
            if (!id || id == local || member.platform != 0 || !std::isfinite(member.distance) || member.distance < 0) continue;
            if (snapshot.customRoom || unlimited) eligible.emplace(id, 0);
            else if (member.distance <= radius * (peers.contains(id) ? 1.15 : 1.0)) eligible.emplace(id, member.distance);
        }
        for (auto it = peers.begin(); it != peers.end();) {
            if (!eligible.contains(it->first)) { End(it->first, it->second); it = peers.erase(it); }
            else ++it;
        }
        for (const auto& [id, distance] : eligible) {
            auto [it, added] = peers.try_emplace(id);
            auto& peer = it->second;
            if (added && !Begin(id, peer, now)) { peers.erase(it); continue; }
            const auto controls = preferences(id);
            const float gain = unlimited ? 1.0f : ProximityGain(distance, radius);
            if (controls.muted != peer.controls.muted || controls.volume != peer.controls.volume ||
                (gain == 0 && peer.gain > 0)) ClearAudio(peer);
            peer.controls = {std::clamp(controls.volume, 0, 100), controls.muted};
            peer.distance = distance; peer.gain = gain;
        }
    }
    void Tick(uint64_t now, bool microphoneOff) {
        for (auto& [id, peer] : peers) {
            if (!peer.handshake.Hello() && !Begin(id, peer, now)) continue;
            if (peer.handshake.Ready() && now - peer.lastAck > 6000) {
                End(id, peer);
                if (!Begin(id, peer, now)) continue;
            }
            if (now >= peer.nextHello) {
                if (const auto hello = peer.handshake.Hello()) {
                    if (send(id, *hello, true)) ++peer.helloSent;
                    else ++peer.helloFailed;
                }
                peer.nextHello = now + (peer.handshake.Ready() ? 2000 : now - peer.startedAt < 10000 ? 1000 : 5000);
            }
            if (!peer.handshake.Ready()) continue;
            const int muted = microphoneOff || peer.controls.muted || peer.gain <= 0 ? 1 : 0;
            if (muted != peer.lastSentMic || now >= peer.nextMic) {
                if (auto packet = peer.handshake.DataHeader(LoungeProtocol::Type::MicState)) {
                    LoungeProtocol::AppendSequence(*packet, ++peer.sendMicSequence);
                    packet->push_back(static_cast<uint8_t>(muted));
                    send(id, *packet, false);
                }
                peer.lastSentMic = muted; peer.nextMic = now + 500;
            }
        }
    }
    void Receive(uint64_t sender, std::span<const uint8_t> bytes, uint64_t now) {
        const auto found = peers.find(sender);
        if (found == peers.end()) { ++unknownSenders; return; } // Only current roster members.
        auto& peer = found->second;
        ++peer.wireReceived;
        const auto packet = peer.handshake.Decode(bytes);
        if (!packet) { ++peer.rejected; return; }
        using LoungeProtocol::Type;
        if (packet->type == Type::Audio || packet->type == Type::MicState) {
            if (!peer.handshake.AcceptData(sender, bytes, packet->type)) { ++peer.rejected; return; }
            const auto sequence = LoungeProtocol::Sequence(bytes);
            if (packet->type == Type::MicState) {
                const auto muted = bytes[LoungeProtocol::HeaderSize + 4];
                if (muted > 1 || (peer.haveMicSequence && !NewSequence(sequence, peer.remoteMicSequence))) return;
                const bool newlyMuted = muted && peer.micState != 1;
                peer.haveMicSequence = true; peer.remoteMicSequence = sequence;
                peer.micState = muted; peer.micStateAt = now;
                if (newlyMuted) ClearAudio(peer);
                return;
            }
            if (!peer.audioSequences.Accept(sequence) || peer.controls.muted || peer.controls.volume == 0 || peer.gain <= 0 ||
                (peer.micState == 1 && now - peer.micStateAt < 2500)) return;
            VoiceBlock pcm;
            int peak = 0;
            for (size_t i = 0; i < pcm.size(); ++i) {
                const auto offset = LoungeProtocol::HeaderSize + 4 + i * 2;
                const uint16_t value = uint16_t(bytes[offset]) | (uint16_t(bytes[offset + 1]) << 8);
                memcpy(&pcm[i], &value, sizeof(value));
                peak = std::max(peak, std::abs(int(pcm[i])));
            }
            peer.jitter.Push(sequence, pcm, now);
            peer.level = peak / 32768.0f * peer.gain * peer.controls.volume / 100.0f;
            peer.levelAt = now;
            if (peer.level > .025f) peer.spokeAt = now;
            ++peer.received;
        } else {
            const bool wasReady = peer.handshake.Ready();
            const auto receipt = peer.handshake.Receive(sender, bytes);
            if (receipt.epochChanged) NewEpoch(peer);
            if (receipt.reply) send(sender, *receipt.reply, true);
            if (receipt.acknowledged) {
                peer.lastAck = now;
                if (!wasReady || receipt.epochChanged) peer.connectedAt = now;
            }
            if (receipt.closed) { NewEpoch(peer); peer.nextHello = now + 1000; }
        }
    }
    void Capture(const int16_t* pcm) {
        for (auto& [id, peer] : peers) {
            if (!peer.handshake.Ready() || peer.gain <= 0 || peer.controls.muted) continue;
            auto packet = peer.handshake.DataHeader(LoungeProtocol::Type::Audio);
            if (!packet) continue;
            LoungeProtocol::AppendSequence(*packet, ++peer.sendSequence);
            for (size_t i = 0; i < LoungeProtocol::Samples; ++i) {
                const auto value = static_cast<uint16_t>(pcm[i]);
                packet->push_back(static_cast<uint8_t>(value));
                packet->push_back(static_cast<uint8_t>(value >> 8));
            }
            if (send(id, *packet, false)) ++peer.sent;
            else ++peer.failed;
        }
    }
    std::optional<VoiceBlock> Mix(uint64_t now) {
        if (now < nextMix) return {};
        // No catch-up burst: discard old device/audio latency after a stalled worker.
        if (nextMix && now - nextMix > 100) {
            for (auto& [id, peer] : peers) ClearAudio(peer);
        }
        // Advance the sample clock, not the (coarsely scheduled) polling clock.
        // The caller drains due blocks, bounded to the device queue capacity.
        if (!nextMix || now - nextMix > 100) nextMix = now;
        nextMix += 20;
        VoiceMix mix;
        bool connected = false;
        for (auto& [id, peer] : peers) {
            if (!peer.handshake.Ready()) continue;
            connected = true;
            const auto pcm = peer.jitter.Pop(now);
            if (!peer.controls.muted) mix.Add(pcm, peer.gain * peer.controls.volume / 100.0f);
        }
        return connected ? std::optional<VoiceBlock>(mix.Finish()) : std::nullopt;
    }
};
}
