#pragma once
#include "LoungeReader.h"
#include <array>
#include <charconv>
#include <cstring>
#include <unordered_set>

namespace VoiceChat {
struct CustomLobbyLayout {
    uint32_t rootRva = 0, ownerVtable = 0, sessionVtable = 0, idVtable = 0, memberVtable = 0;
    uint32_t peTimestamp = 0, imageSize = 0;
};

// Read-only custom-room snapshot. Every observed field is reread before publishing.
class CustomLobbyReader {
    uintptr_t base;
    CustomLobbyLayout layout;
    LoungeMemoryRead read;
    void* context;
    struct Observation { uintptr_t at; std::vector<uint8_t> bytes; };
    struct Capture {
        const CustomLobbyReader& reader;
        std::vector<Observation> observations;
        void Bytes(uintptr_t at, void* output, size_t size) {
            if (at < 0x10000 || at > 0x00007FFFFFFEFFFFULL || size > 4096 || observations.size() >= 8192)
                throw LoungeReadStatus::InvalidData;
            if (!reader.read(reader.context, at, output, size)) throw LoungeReadStatus::Unreadable;
            const auto* bytes = static_cast<const uint8_t*>(output);
            observations.push_back({at, {bytes, bytes + size}});
        }
        template<class T> T Get(uintptr_t at) { T value{}; Bytes(at, &value, sizeof(value)); return value; }
        uintptr_t Pointer(uintptr_t at) {
            const auto p = Get<uintptr_t>(at);
            if (p < 0x10000 || p > 0x00007FFFFFFEFFFFULL) throw LoungeReadStatus::InvalidData;
            return p;
        }
        void Vtable(uintptr_t object, uint32_t rva) {
            if (Get<uintptr_t>(object) != reader.base + rva) throw LoungeReadStatus::UnsupportedLayout;
        }
        std::string String(uintptr_t at, size_t max = 128) {
            const auto length = Get<uint64_t>(at + 16), capacity = Get<uint64_t>(at + 24);
            if (length > max || capacity < length || capacity > 4096 || (capacity < 16 && capacity != 15))
                throw LoungeReadStatus::InvalidData;
            const auto data = capacity >= 16 ? Pointer(at) : at;
            std::string value(static_cast<size_t>(length) + 1, '\0');
            Bytes(data, value.data(), value.size());
            if (value.back() != '\0') throw LoungeReadStatus::InvalidData;
            value.pop_back();
            for (unsigned char ch : value) if (ch < 32 || ch == 127) throw LoungeReadStatus::InvalidData;
            return value;
        }
        void Verify() {
            for (const auto& observation : observations) {
                std::vector<uint8_t> current(observation.bytes.size());
                if (!reader.read(reader.context, observation.at, current.data(), current.size()))
                    throw LoungeReadStatus::Unreadable;
                if (current != observation.bytes) throw LoungeReadStatus::ChangedDuringRead;
            }
        }
    };
public:
    CustomLobbyReader(uintptr_t b, CustomLobbyLayout l, LoungeMemoryRead r, void* c)
        : base(b), layout(l), read(r), context(c) {}
    bool StillCurrent(const LoungeSnapshot& snapshot) const {
        uintptr_t root = 0, owner = 0, holder = 0, session = 0;
        const auto ptr = [&](uintptr_t at, uintptr_t& value) {
            return at >= 0x10000 && at <= 0x00007FFFFFFEFFFFULL &&
                read && read(context, at, &value, sizeof(value));
        };
        return snapshot.customRoom && snapshot.status == LoungeReadStatus::Ok &&
            ptr(base + layout.rootRva, root) && ptr(root + 0x640, owner) &&
            ptr(owner + 0x730, holder) && ptr(holder, session) && session == snapshot.world;
    }
    LoungeSnapshot Read() const {
        using Status = LoungeReadStatus;
        try {
            if (!base || !read || !layout.rootRva || !layout.peTimestamp || layout.imageSize < 0x1000)
                throw Status::UnsupportedBuild;
            for (auto rva : {layout.rootRva, layout.ownerVtable, layout.sessionVtable, layout.idVtable, layout.memberVtable})
                if (!rva || rva > layout.imageSize - 8) throw Status::UnsupportedBuild;
            Capture c{*this};
            if (c.Get<uint16_t>(base) != 0x5A4D) throw Status::UnsupportedBuild;
            const auto pe = c.Get<uint32_t>(base + 0x3C);
            if (pe > 0x1000 || c.Get<uint32_t>(base + pe) != 0x4550 ||
                c.Get<uint16_t>(base + pe + 4) != 0x8664 ||
                c.Get<uint32_t>(base + pe + 8) != layout.peTimestamp ||
                c.Get<uint32_t>(base + pe + 0x50) != layout.imageSize) throw Status::UnsupportedBuild;
            const auto root = c.Get<uintptr_t>(base + layout.rootRva);
            if (!root) throw Status::NotReady;
            const auto owner = c.Pointer(root + 0x640);
            c.Vtable(owner, layout.ownerVtable);
            const auto holder = c.Get<uintptr_t>(owner + 0x730);
            if (!holder) { c.Verify(); return {Status::NotInLounge}; }
            const auto session = c.Pointer(holder);
            c.Vtable(session, layout.sessionVtable);
            const auto id = c.Pointer(session + 8);
            c.Vtable(id, layout.idVtable);
            const auto key = c.String(id + 8, 64);
            // Other session types share the manager. Only validated Player Match IDs.
            if (!key.starts_with("PLM")) { c.Verify(); return {Status::NotInLounge}; }
            if (key.size() < 4) throw Status::InvalidData;
            for (char ch : key.substr(3))
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) throw Status::InvalidData;
            LoungeSnapshot result;
            result.status = Status::Ok; result.customRoom = true;
            result.world = session; result.roomKey = "custom:" + key;
            const auto head = c.Pointer(session + 0x180);
            const auto count = c.Get<uint64_t>(session + 0x188);
            if (!count || count > 64) throw Status::InvalidData;
            auto node = c.Pointer(head), previous = head;
            std::unordered_set<uintptr_t> nodes;
            std::unordered_set<std::string> users;
            std::unordered_set<uint64_t> steamIds;
            for (uint64_t i = 0; i < count; ++i) {
                if (node == head || !nodes.insert(node).second || c.Pointer(node + 8) != previous)
                    throw Status::InvalidData;
                const auto user = c.String(node + 0x10, 64);
                const auto record = c.Pointer(node + 0x30);
                c.Vtable(record, layout.memberVtable);
                if (user.empty() || c.String(record + 8, 64) != user || !users.insert(user).second)
                    throw Status::InvalidData;
                LoungeParticipant member;
                member.userId = user; member.name = c.String(record + 0x28);
                const auto steam = c.String(record + 0x70, 64), duplicate = c.String(record + 0x90, 64);
                uint64_t value = 0;
                const auto parsed = std::from_chars(steam.data(), steam.data() + steam.size(), value);
                if (steam == duplicate && parsed.ec == std::errc{} && parsed.ptr == steam.data() + steam.size() &&
                    (value >> 32) == 0x01100001ULL && uint32_t(value) && std::to_string(value) == steam) {
                    if (!steamIds.insert(value).second) throw Status::InvalidData;
                    member.steamIdCandidate = value;
                    member.platform = 0; // Normalized Steam endpoint, not the game's native platform enum.
                }
                result.participants.push_back(std::move(member));
                previous = node; node = c.Pointer(node);
            }
            if (node != head || c.Pointer(head + 8) != previous) throw Status::InvalidData;
            c.Verify();
            return result;
        } catch (LoungeReadStatus status) { return {status}; }
    }
};
}
