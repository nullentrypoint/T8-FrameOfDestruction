#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace VoiceChat::ProbeProtocol {
constexpr int Channel = 21560;
constexpr size_t PacketSize = 80;
using Nonce = std::array<uint8_t, 16>;
using TekkenId = std::array<char, 12>;
using Wire = std::array<uint8_t, PacketSize>;
enum class Type : uint8_t { Hello = 1, Ack = 2, Bye = 3, Audio = 4, MicState = 5 };
struct Pair {
    uint64_t localSteam = 0, remoteSteam = 0;
    TekkenId localTekken{}, remoteTekken{};
    bool operator==(const Pair&) const = default;
};
struct Packet {
    Type type{};
    uint64_t sender = 0, recipient = 0;
    Nonce nonce{}, echo{};
    TekkenId senderTekken{}, recipientTekken{};
};
inline bool Empty(const Nonce& nonce) {
    return std::all_of(nonce.begin(), nonce.end(), [](uint8_t b) { return b == 0; });
}
inline Wire Encode(const Packet& p) {
    Wire bytes{};
    memcpy(bytes.data(), "T8VP", 4);
    bytes[4] = 1; // Protocol version, little-endian uint16.
    bytes[6] = static_cast<uint8_t>(p.type);
    for (size_t i = 0; i < 8; ++i) {
        bytes[8 + i] = static_cast<uint8_t>(p.sender >> (8 * i));
        bytes[16 + i] = static_cast<uint8_t>(p.recipient >> (8 * i));
    }
    memcpy(bytes.data() + 24, p.nonce.data(), 16);
    memcpy(bytes.data() + 40, p.echo.data(), 16);
    memcpy(bytes.data() + 56, p.senderTekken.data(), 12);
    memcpy(bytes.data() + 68, p.recipientTekken.data(), 12);
    return bytes;
}
inline std::optional<Packet> Decode(std::span<const uint8_t> bytes) {
    if (bytes.size() != PacketSize || memcmp(bytes.data(), "T8VP", 4) || bytes[4] != 1 || bytes[5] || bytes[7] ||
        bytes[6] < 1 || bytes[6] > 5) return {};
    Packet p;
    p.type = static_cast<Type>(bytes[6]);
    for (size_t i = 0; i < 8; ++i) {
        p.sender |= uint64_t(bytes[8 + i]) << (8 * i);
        p.recipient |= uint64_t(bytes[16 + i]) << (8 * i);
    }
    memcpy(p.nonce.data(), bytes.data() + 24, 16);
    memcpy(p.echo.data(), bytes.data() + 40, 16);
    memcpy(p.senderTekken.data(), bytes.data() + 56, 12);
    memcpy(p.recipientTekken.data(), bytes.data() + 68, 12);
    if (Empty(p.nonce)) return {};
    return p;
}

class Handshake {
    Pair pair{};
    Nonce nonce{}, peerNonce{};
    std::array<Nonce, 16> retired{};
    size_t retiredCount = 0;
    bool active = false, ready = false;
    Packet Make(Type type, const Nonce& echo) const {
        return {type, pair.localSteam, pair.remoteSteam, nonce, echo, pair.localTekken, pair.remoteTekken};
    }
    bool Retired(const Nonce& value) const {
        return std::find(retired.begin(), retired.begin() + retiredCount, value) != retired.begin() + retiredCount;
    }
public:
    struct Receipt {
        std::optional<Wire> reply;
        bool acknowledged = false;
        bool peerClosed = false;
    };
    void Begin(const Pair& selected, const Nonce& freshNonce) {
        pair = selected;
        nonce = freshNonce;
        peerNonce = {};
        retiredCount = 0;
        ready = false;
        active = !Empty(nonce) && pair.localSteam && pair.remoteSteam && pair.localSteam != pair.remoteSteam;
    }
    bool Ready() const { return active && ready; }
    std::optional<Wire> AudioHeader() const {
        if (!Ready()) return {};
        return Encode(Make(Type::Audio, peerNonce));
    }
    bool AcceptAudio(uint64_t sender, std::span<const uint8_t> bytes) const {
        return AcceptData(sender, bytes, Type::Audio);
    }
    std::optional<Wire> MicStateHeader() const {
        if (!Ready()) return {};
        return Encode(Make(Type::MicState, peerNonce));
    }
    bool AcceptData(uint64_t sender, std::span<const uint8_t> bytes, Type type) const {
        const auto p = Decode(bytes);
        return Ready() && p && p->type == type && sender == pair.remoteSteam &&
            p->sender == pair.remoteSteam && p->recipient == pair.localSteam &&
            p->senderTekken == pair.remoteTekken && p->recipientTekken == pair.localTekken &&
            p->nonce == peerNonce && p->echo == nonce;
    }
    void MarkTimedOut() { ready = false; }
    std::optional<Wire> Hello() const {
        if (!active) return {};
        return Encode(Make(Type::Hello, {}));
    }
    std::optional<Wire> End() {
        std::optional<Wire> bye;
        if (active && !Empty(peerNonce)) bye = Encode(Make(Type::Bye, peerNonce));
        active = ready = false;
        return bye;
    }
    Receipt Receive(uint64_t authenticatedSender, std::span<const uint8_t> bytes) {
        const auto decoded = Decode(bytes);
        if (!active || !decoded) return {};
        const auto& p = *decoded;
        if (p.type == Type::Audio || p.type == Type::MicState) return {};
        if (authenticatedSender != pair.remoteSteam || p.sender != pair.remoteSteam || p.recipient != pair.localSteam ||
            p.senderTekken != pair.remoteTekken || p.recipientTekken != pair.localTekken || Retired(p.nonce)) return {};
        if (p.type == Type::Hello) {
            if (!Empty(p.echo)) return {};
            // HELLO alone never establishes readiness, including a delayed HELLO.
            return {Encode(Make(Type::Ack, p.nonce)), false, false};
        }
        if (p.echo != nonce) return {};
        if (p.type == Type::Bye) {
            if (Empty(peerNonce) || p.nonce != peerNonce || retiredCount == retired.size()) return {};
            retired[retiredCount++] = peerNonce;
            peerNonce = {};
            ready = false;
            return {{}, false, true};
        }
        if (p.nonce != peerNonce && !Empty(peerNonce)) {
            if (retiredCount == retired.size()) return {};
            retired[retiredCount++] = peerNonce;
        }
        peerNonce = p.nonce;
        ready = true;
        return {{}, true, false};
    }
};
}
