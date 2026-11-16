#include "LoungeReader.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace VoiceChat {
namespace {
using Status = LoungeReadStatus;
struct ArrayHeader {
    uintptr_t data;
    int32_t count, capacity;
};
static_assert(sizeof(ArrayHeader) == 16);
struct MapHeader {
    ArrayHeader slots;
    std::array<uint32_t, 4> inlineBits;
    uintptr_t heapBits;
    int32_t bitCount, bitCapacity, firstFree, freeCount;
    uint64_t inlineHash;
    uintptr_t hash;
    int32_t hashSize, padding;
};
static_assert(sizeof(MapHeader) == 80);
struct MapEntry { uint64_t key; uintptr_t actor; int32_t hashNext, hashIndex; };
static_assert(sizeof(MapEntry) == 24);

class Capture {
    struct Observation { uintptr_t address; std::vector<unsigned char> bytes; };
    LoungeMemoryRead read;
    void* context;
    uintptr_t base;
    LoungeLayout layout;
    std::vector<Observation> observations;
    std::unordered_map<uint32_t, std::string> names;
    std::unordered_map<uintptr_t, std::string> classes;

    void Bytes(uintptr_t address, void* output, size_t size, bool stable = true) {
        if (address < 0x10000 || address > (std::numeric_limits<uintptr_t>::max)() - size)
            throw Status::InvalidData;
        if (!read(context, address, output, size)) throw Status::Unreadable;
        if (stable) {
            if (observations.size() >= 20000) throw Status::InvalidData;
            const auto bytes = static_cast<const unsigned char*>(output);
            observations.push_back({address, {bytes, bytes + size}});
        }
    }
    template<class T> T Get(uintptr_t address, bool stable = true) {
        T value{}; Bytes(address, &value, sizeof(value), stable); return value;
    }
    uintptr_t Pointer(uintptr_t address) {
        const auto value = Get<uintptr_t>(address);
        if (!value) throw Status::NotReady;
        // Bounds also prevent arithmetic wrap in the fixed-offset traversals.
        if (value < 0x10000 || value > 0x00007FFFFFFEFFFFULL) throw Status::InvalidData;
        return value;
    }
    static std::string Utf8(const std::vector<uint16_t>& text) {
        std::string result;
        for (size_t i = 0; i < text.size(); ++i) {
            uint32_t cp = text[i];
            if (!cp) throw Status::InvalidData;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (++i >= text.size() || text[i] < 0xDC00 || text[i] > 0xDFFF) throw Status::InvalidData;
                cp = 0x10000 + ((cp - 0xD800) << 10) + text[i] - 0xDC00;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) throw Status::InvalidData;
            if (cp < 0x80) result += static_cast<char>(cp);
            else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 63));
            } else if (cp < 0x10000) {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                result += static_cast<char>(0x80 | (cp & 63));
            } else {
                result += static_cast<char>(0xF0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 63));
                result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                result += static_cast<char>(0x80 | (cp & 63));
            }
        }
        return result;
    }
    std::string Name(uint32_t id) {
        if (const auto it = names.find(id); it != names.end()) return it->second;
        const auto blockIndex = id >> 16;
        if (blockIndex >= 8192) throw Status::InvalidData;
        const auto block = Pointer(base + layout.nameBlocksRva + blockIndex * 8);
        const auto entry = block + (id & 0xFFFF) * 2;
        const auto header = Get<uint16_t>(entry);
        const size_t length = header >> 6;
        if (!length || length > 256) throw Status::InvalidData;
        std::string value;
        if (header & 1) {
            std::vector<uint16_t> text(length);
            Bytes(entry + 2, text.data(), length * 2);
            value = Utf8(text);
        } else {
            value.resize(length);
            Bytes(entry + 2, value.data(), length);
            for (const unsigned char ch : value) if (ch < 32 || ch >= 127) throw Status::InvalidData;
        }
        names.emplace(id, value);
        return value;
    }
    std::string Class(uintptr_t object) {
        const auto type = Pointer(object + 0x10);
        if (const auto it = classes.find(type); it != classes.end()) return it->second;
        auto value = Name(Get<uint32_t>(type + 0x18));
        classes.emplace(type, value);
        return value;
    }
    void RequireClass(uintptr_t object, const char* expected) {
        if (Class(object) != expected) throw Status::InvalidData;
    }
    std::string String(uintptr_t address) {
        const auto h = Get<ArrayHeader>(address);
        if (h.count < 0 || h.count > 256 || h.capacity < h.count || h.capacity > 4096)
            throw Status::InvalidData;
        if (!h.count) return {};
        std::vector<uint16_t> text(h.count);
        Bytes(h.data, text.data(), text.size() * 2);
        if (text.back()) throw Status::InvalidData;
        text.pop_back();
        return Utf8(text);
    }
    LoungePosition Position(uintptr_t actor) {
        const auto root = Pointer(actor + 0x1A0);
        // ComponentToWorld translation used by the game's GetArea implementation.
        const auto p = Get<LoungePosition>(root + 0x260, false);
        for (const auto v : {p.x, p.y, p.z})
            if (!std::isfinite(v) || std::abs(v) > 1e7) throw Status::InvalidData;
        return p;
    }
    static uint64_t SteamCandidate(const std::string& text) {
        if (text.size() != 17) return 0;
        uint64_t id = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return 0;
        return (id >> 56) == 1 && ((id >> 52) & 15) == 1 &&
            ((id >> 32) & 0xFFFFF) == 1 && uint32_t(id) != 0 ? id : 0;
    }
    void Verify() {
        for (const auto& observed : observations) {
            std::vector<unsigned char> current(observed.bytes.size());
            if (!read(context, observed.address, current.data(), current.size())) throw Status::Unreadable;
            if (current != observed.bytes) throw Status::ChangedDuringRead;
        }
    }
public:
    Capture(uintptr_t b, LoungeLayout l, LoungeMemoryRead r, void* ctx)
        : read(r), context(ctx), base(b), layout(l) {}
    LoungeSnapshot Run() {
        if (!read || !base || !layout.worldRva || !layout.nameBlocksRva || !layout.subsystemRva ||
            !layout.peTimestamp || layout.imageSize < 0x10000 ||
            base > 0x00007FFFFFFEFFFFULL - layout.imageSize ||
            layout.worldRva > layout.imageSize - 8 || layout.subsystemRva > layout.imageSize - 8 ||
            layout.nameBlocksRva > layout.imageSize - 8192 * 8)
            throw Status::UnsupportedBuild;
        if (Get<uint16_t>(base) != 0x5A4D) throw Status::UnsupportedBuild;
        const auto peOffset = Get<uint32_t>(base + 0x3C);
        if (peOffset > 0x1000 || Get<uint32_t>(base + peOffset) != 0x4550 ||
            Get<uint16_t>(base + peOffset + 4) != 0x8664 ||
            Get<uint32_t>(base + peOffset + 8) != layout.peTimestamp ||
            Get<uint16_t>(base + peOffset + 24) != 0x20B ||
            Get<uint32_t>(base + peOffset + 0x50) != layout.imageSize)
            throw Status::UnsupportedBuild;
        if (Name(0) != "None") throw Status::UnsupportedBuild;
        LoungeSnapshot result;
        result.world = Pointer(base + layout.worldRva);
        const auto mode = Pointer(result.world + 0x150);
        result.gameMode = mode;
        // The room and network flag remain populated in the main menu.
        if (Class(mode) != "BP_LobbyGameMode_Onl_C") {
            Verify(); return {Status::NotInLounge};
        }
        if (Get<uint8_t>(mode + 0x3EE) != 1 || Get<uint8_t>(mode + 0x3EF) != 1)
            throw Status::NotReady;
        const auto subsystem = Pointer(base + layout.subsystemRva);
        RequireClass(subsystem, "LobbyGameInstanceSubsystem");
        if (Get<uint8_t>(subsystem + 0x1B1) != 1) throw Status::NotReady;
        const auto network = Pointer(subsystem + 0x2A0);
        RequireClass(network, "LobbyNetworkObject");
        result.roomKey = String(network + 0x140);
        if (result.roomKey.empty()) throw Status::NotReady;
        const auto instance = Pointer(result.world + 0x1B8);
        const auto locals = Get<ArrayHeader>(instance + 0x38);
        if (locals.count != 1 || locals.capacity < 1 || locals.capacity > 16) throw Status::InvalidData;
        const auto local = Pointer(locals.data);
        const auto controller = Pointer(local + 0x30);
        result.localActor = Pointer(controller + 0x340);
        RequireClass(result.localActor, "BP_Player_Onl_C");
        result.localPosition = Position(result.localActor);
        const auto manager = Pointer(mode + 0x488);
        RequireClass(manager, "LobbyEnemyManager");
        const auto map = Get<MapHeader>(manager + 0x28);
        if (map.slots.count < 0 || map.slots.capacity < map.slots.count || map.slots.capacity > 4096 ||
            map.bitCount != map.slots.count || map.freeCount < 0 || map.freeCount > map.slots.count ||
            (map.freeCount && (map.firstFree < 0 || map.firstFree >= map.slots.count)))
            throw Status::InvalidData;
        // Only the observed inline allocator layout is supported in this prototype.
        if (map.bitCapacity != 128 || map.slots.count > 128 || map.heapBits)
            throw Status::UnsupportedLayout;
        int occupied = 0;
        std::unordered_set<uint64_t> ids;
        std::unordered_set<uintptr_t> actors;
        std::unordered_set<uint64_t> steamIds;
        for (int i = 0; i < map.slots.count; ++i) {
            if (!(map.inlineBits[i / 32] & (uint32_t{1} << (i % 32)))) continue;
            ++occupied;
            if (map.slots.data > (std::numeric_limits<uintptr_t>::max)() - 128 * sizeof(MapEntry))
                throw Status::InvalidData;
            const auto entry = Get<MapEntry>(map.slots.data + i * sizeof(MapEntry));
            if (!entry.key || !ids.insert(entry.key).second || !actors.insert(entry.actor).second ||
                entry.actor < 0x10000 || entry.actor > 0x00007FFFFFFEFFFFULL)
                throw Status::InvalidData;
            RequireClass(entry.actor, "BP_AvatarEnemy_Onl_C");
            LoungeParticipant peer;
            peer.actor = entry.actor;
            peer.cosmosId = Get<uint64_t>(peer.actor + 0x390);
            if (peer.cosmosId != entry.key) throw Status::InvalidData;
            peer.platform = Get<int32_t>(peer.actor + 0x2A0);
            peer.userId = Name(Get<uint32_t>(peer.actor + 0x2D8));
            if (Get<uint32_t>(peer.actor + 0x2DC) != 0) throw Status::InvalidData;
            const auto display = Get<ArrayHeader>(peer.actor + 0x380);
            if (display.count != 3 || display.capacity < 3 || display.capacity > 64 ||
                display.data < 0x10000 || display.data > 0x00007FFFFFFEFFFFULL) throw Status::InvalidData;
            peer.name = String(display.data);
            const auto platformId = String(display.data + 16);
            if (peer.platform == 0) peer.steamIdCandidate = SteamCandidate(platformId);
            if (peer.steamIdCandidate && !steamIds.insert(peer.steamIdCandidate).second) throw Status::InvalidData;
            peer.position = Position(peer.actor);
            peer.distance = std::hypot(peer.position.x - result.localPosition.x,
                peer.position.y - result.localPosition.y, peer.position.z - result.localPosition.z);
            result.participants.push_back(std::move(peer));
        }
        if (occupied != map.slots.count - map.freeCount) throw Status::InvalidData;
        Verify();
        std::sort(result.participants.begin(), result.participants.end(), [](const auto& a, const auto& b) {
            return a.distance < b.distance;
        });
        result.status = Status::Ok;
        return result;
    }
};
}

const char* LoungeStatusName(LoungeReadStatus status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::NotInLounge: return "not-in-lounge";
    case Status::NotReady: return "not-ready";
    case Status::UnsupportedBuild: return "unsupported-build";
    case Status::UnsupportedLayout: return "unsupported-layout";
    case Status::Unreadable: return "unreadable";
    case Status::InvalidData: return "invalid-data";
    case Status::ChangedDuringRead: return "changed-during-read";
    }
    return "unknown";
}
LoungeReader::LoungeReader(uintptr_t b, LoungeLayout l, LoungeMemoryRead r, void* ctx)
    : base(b), layout(l), read(r), context(ctx) {}
LoungeSnapshot LoungeReader::Read() const {
    try { return Capture(base, layout, read, context).Run(); }
    catch (LoungeReadStatus status) { return {status}; }
}
}
