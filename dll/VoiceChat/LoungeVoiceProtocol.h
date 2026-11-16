#pragma once
#include "SteamProbeProtocol.h"
#include <string>
#include <vector>

namespace VoiceChat::LoungeProtocol {
// Separate channel and envelope: old match clients cannot accept lounge audio.
constexpr int Channel = 21561;
constexpr size_t RoomCapacity = 128, PrefixSize = 136;
constexpr size_t HeaderSize = PrefixSize + ProbeProtocol::PacketSize;
constexpr size_t Samples = 320, AudioSize = HeaderSize + 4 + Samples * 2;
using Bytes = std::vector<uint8_t>;
using ProbeProtocol::Nonce;
using ProbeProtocol::Type;

inline uint32_t Sequence(std::span<const uint8_t> bytes) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[HeaderSize + i]) << (8 * i);
    return value;
}
inline void AppendSequence(Bytes& bytes, uint32_t sequence) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(sequence >> (8 * i)));
}
inline bool ValidRoom(const std::string& room) {
    return !room.empty() && room.size() <= RoomCapacity &&
        std::all_of(room.begin(), room.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
class Handshake {
    ProbeProtocol::Handshake handshake;
    std::string room;
    Bytes Wrap(const ProbeProtocol::Wire& inner) const {
        Bytes bytes(HeaderSize);
        memcpy(bytes.data(), "T8LV", 4);
        bytes[4] = 1;
        bytes[6] = static_cast<uint8_t>(room.size());
        memcpy(bytes.data() + 8, room.data(), room.size());
        memcpy(bytes.data() + PrefixSize, inner.data(), inner.size());
        return bytes;
    }
public:
    struct Receipt {
        std::optional<Bytes> reply;
        bool acknowledged = false, closed = false, epochChanged = false;
    };
    bool Begin(uint64_t local, uint64_t remote, const std::string& key, const Nonce& nonce) {
        handshake.End();
        room = key;
        if (!ValidRoom(room)) return false;
        handshake.Begin({local, remote, {}, {}}, nonce);
        return handshake.Hello().has_value();
    }
    bool Ready() const { return handshake.Ready(); }
    std::optional<ProbeProtocol::Packet> Decode(std::span<const uint8_t> bytes) const {
        if (bytes.size() < HeaderSize || memcmp(bytes.data(), "T8LV", 4) ||
            bytes[4] != 1 || bytes[5] || bytes[7] || bytes[6] != room.size() ||
            memcmp(bytes.data() + 8, room.data(), room.size())) return {};
        for (size_t i = 8 + room.size(); i < PrefixSize; ++i) if (bytes[i]) return {};
        const auto packet = ProbeProtocol::Decode(bytes.subspan(PrefixSize, ProbeProtocol::PacketSize));
        if (!packet) return {};
        const size_t expected = packet->type == Type::Audio ? AudioSize :
            packet->type == Type::MicState ? HeaderSize + 5 : HeaderSize;
        if (bytes.size() != expected) return {};
        return packet;
    }
    std::optional<Bytes> Hello() const {
        if (const auto packet = handshake.Hello()) return Wrap(*packet);
        return {};
    }
    std::optional<Bytes> End() {
        if (const auto packet = handshake.End()) return Wrap(*packet);
        return {};
    }
    std::optional<Bytes> DataHeader(Type type) const {
        const auto header = type == Type::Audio ? handshake.AudioHeader() : handshake.MicStateHeader();
        if (header) return Wrap(*header);
        return {};
    }
    bool AcceptData(uint64_t sender, std::span<const uint8_t> bytes, Type type) const {
        return Decode(bytes).has_value() && handshake.AcceptData(sender,
            bytes.subspan(PrefixSize, ProbeProtocol::PacketSize), type);
    }
    Receipt Receive(uint64_t sender, std::span<const uint8_t> bytes) {
        if (!Decode(bytes)) return {};
        const auto before = handshake.AudioHeader();
        const auto receipt = handshake.Receive(sender, bytes.subspan(PrefixSize, ProbeProtocol::PacketSize));
        Receipt result;
        if (receipt.reply) result.reply = Wrap(*receipt.reply);
        result.acknowledged = receipt.acknowledged;
        result.closed = receipt.peerClosed;
        result.epochChanged = before != handshake.AudioHeader();
        return result;
    }
};
}
