#include "VoiceChat/PlayerIdentityReader.h"
#include "VoiceChat/IdentityTransitions.h"
#include "VoiceChat/IdentitySession.h"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace {
using namespace VoiceChat;
// Fixed synthetic identities keep the fixtures reproducible without captured player data.
struct Fixture {
    static constexpr uintptr_t Base = 0x10000000;
    static constexpr uintptr_t Root = Base + 0x1000;
    static constexpr uintptr_t Vector = Base + 0x1100;
    static constexpr uintptr_t Local = Base + 0x2000;
    static constexpr uintptr_t Remote = Base + 0x3000;
    IdentityLayout layout{0x100, 0x200, 0x300};
    std::array<unsigned char, 65536> data{};
    uintptr_t heap = Base + 0x5000;
    uintptr_t changeAddress = 0;
    int changeRead = 0;
    int matchingReads = 0;

    template<typename T> void Put(uintptr_t address, T value) {
        assert(address >= Base && address - Base + sizeof(T) <= data.size());
        memcpy(data.data() + address - Base, &value, sizeof(T));
    }
    uintptr_t Text(uintptr_t address, const char* value) {
        const size_t length = strlen(value);
        uintptr_t storage = address;
        if (length >= 16) {
            storage = heap;
            heap += 0x100;
            Put(address, storage);
        }
        memcpy(data.data() + storage - Base, value, length + 1);
        Put<uint64_t>(address + 16, length);
        Put<uint64_t>(address + 24, length < 16 ? 15 : 63);
        return storage;
    }
    void Player(uintptr_t address, uint32_t role, const char* tekken, const char* a, const char* b) {
        Put(address, Base + layout.vtableRva);
        Put(address + 8, role);
        Put<uint32_t>(address + 12, 4);
        Put<uint32_t>(address + 16, 15);
        Put<uint32_t>(address + 20, 255);
        Text(address + 0x60, "628491735206819453");
        Text(address + 0x80, tekken);
        Text(address + 0xA0, a);
        Text(address + 0xC0, b);
    }
    Fixture() {
        Put<uint16_t>(Base, 0x5A4D);
        Put(Base + layout.rootRva, Root);
        Put(Base + layout.vtableRva, Base + layout.destructorRva);
        Put(Root, Vector);
        Put(Root + 8, Vector + 16);
        Put(Root + 16, Vector + 16);
        Put(Vector, Local);
        Put(Vector + 8, Remote);
        Player(Local, 1, "7rQ2mK9vXp4A", "76561202131974853", "76561202131974853");
        Player(Remote, 3, "6tN8bR3wYk5C", "76561202084629517", "76561202084629517");
    }
    static bool Read(void* context, uintptr_t address, void* output, size_t size) {
        auto& f = *static_cast<Fixture*>(context);
        if (address < Base || address - Base > f.data.size() || size > f.data.size() - (address - Base)) return false;
        memcpy(output, f.data.data() + address - Base, size);
        if (address == f.changeAddress && ++f.matchingReads == f.changeRead && size)
            static_cast<unsigned char*>(output)[0] ^= 1;
        return true;
    }
    PlayerIdentityReader Reader() { return PlayerIdentityReader(Base, Read, this, layout); }
};
}

void TestPlayerIdentityReader() {
    using namespace VoiceChat;
    Fixture f;
    auto reader = f.Reader();
    auto value = reader.Read();
    assert(value.status == IdentityReadStatus::Ok && value.players.size() == 2);
    assert(value.players[1].ids[1] == "6tN8bR3wYk5C" && value.players[1].ids[2] == "76561202084629517");
    IdentityTransitions transitions;
    auto events = transitions.Update(value);
    assert(events.size() == 2 && events[0].appeared && events[1].appeared);
    assert(transitions.Update(value).empty());
    auto moved = value;
    moved.players[1].address += 0x100;
    moved.players[1].rawWords[2] = 22;
    assert(transitions.Update(moved).empty());
    assert(transitions.Update({IdentityReadStatus::Unreadable, 0, {}}).empty());
    auto menu = value;
    menu.players.resize(1);
    PlayerIdentity placeholder;
    placeholder.address = Fixture::Remote;
    menu.players.push_back(placeholder);
    events = transitions.Update(menu);
    assert(events.size() == 1 && !events[0].appeared);
    assert(events[0].player.ids[2] == "76561202084629517");
    const auto detail = IdentityNotificationDetail(events[0].player);
    assert(detail.find("6tN8-bR3w-Yk5C") != std::string::npos);
    assert(detail.find("76561202084629517") != std::string::npos);
    assert(transitions.Update(menu).empty());
    f.Player(Fixture::Remote, 3, "8pD4sH7zWq2E", "test_q7m4", "8274619503827164953");
    value = reader.Read();
    assert(value.status == IdentityReadStatus::Ok && value.players[1].ids[2] == "test_q7m4");
    assert(value.players[1].ids[3] == "8274619503827164953");
    events = transitions.Update(value);
    assert(events.size() == 1 && events[0].appeared);
    assert(IdentityNotificationDetail(events[0].player).find("Platform A: test_q7m4") != std::string::npos);
    auto replaced = value;
    replaced.players[1].ids = {"492816305749182637", "5xF9cJ2nVr6B", "76561202154817039", "76561202154817039"};
    events = transitions.Update(replaced);
    assert(events.size() == 2 && !events[0].appeared && events[1].appeared);
    assert(events[0].player.ids[2] == "test_q7m4" && events[1].player.ids[2] == "76561202154817039");
    f.Put(Fixture::Root + 8, Fixture::Vector + 8);
    value = reader.Read();
    assert(value.status == IdentityReadStatus::Ok && value.players.size() == 1);
    f.Put<uintptr_t>(Fixture::Base + f.layout.rootRva, 0);
    value = reader.Read();
    assert(value.status == IdentityReadStatus::NotReady && value.players.empty());

    Fixture empty;
    empty.Put<uintptr_t>(Fixture::Root, 0);
    empty.Put<uintptr_t>(Fixture::Root + 8, 0);
    empty.Put<uintptr_t>(Fixture::Root + 16, 0);
    value = empty.Reader().Read();
    assert(value.status == IdentityReadStatus::Ok && value.players.empty());

    Fixture wrongBuild;
    wrongBuild.Put<uintptr_t>(Fixture::Base + wrongBuild.layout.vtableRva, 0);
    value = wrongBuild.Reader().Read();
    assert(value.status == IdentityReadStatus::UnsupportedBuild && value.players.empty());

    Fixture invalid;
    invalid.Put(Fixture::Root + 8, Fixture::Vector - 8);
    assert(invalid.Reader().Read().status == IdentityReadStatus::InvalidData);
    invalid.Put(Fixture::Root + 8, Fixture::Vector + 16);
    invalid.Put<uint64_t>(Fixture::Remote + 0x80 + 16, 65);
    value = invalid.Reader().Read();
    assert(value.status == IdentityReadStatus::InvalidData && value.players.empty());

    Fixture unreadable;
    unreadable.Put<uintptr_t>(Fixture::Remote + 0xA0, Fixture::Base + 0x20000);
    value = unreadable.Reader().Read();
    assert(value.status == IdentityReadStatus::Unreadable && value.players.empty());

    Fixture rootChanged;
    rootChanged.changeAddress = Fixture::Base + rootChanged.layout.rootRva;
    rootChanged.changeRead = 2;
    value = rootChanged.Reader().Read();
    assert(value.status == IdentityReadStatus::ChangedDuringRead && value.players.empty());

    Fixture contentChanged;
    contentChanged.changeAddress = contentChanged.Text(Fixture::Remote + 0xA0, "76561202084629517");
    contentChanged.changeRead = 2;
    value = contentChanged.Reader().Read();
    assert(value.status == IdentityReadStatus::ChangedDuringRead && value.players.empty());

    PlayerIdentityReader overflow((std::numeric_limits<uintptr_t>::max)() - 1, Fixture::Read, &f);
    assert(overflow.Read().status == IdentityReadStatus::UnsupportedBuild);
}

void TestIdentitySession() {
    using namespace VoiceChat;
    Fixture f;
    auto online = f.Reader().Read();
    auto menu = online;
    menu.players.resize(1);
    IdentitySession tracker;
    assert(tracker.Update(menu).empty() && !tracker.HasVerifiedIdentityPair());
    auto events = tracker.Update(online);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Started);
    assert(events[0].session.remoteIds[2] == "76561202084629517");
    const auto firstGeneration = events[0].session.generation;
    auto rematch = online;
    rematch.players[1].address += 0x1000;
    rematch.players[0].rawWords[3] = 7;
    std::swap(rematch.players[0], rematch.players[1]);
    assert(tracker.Update(rematch).empty() && tracker.HasVerifiedIdentityPair());
    assert(tracker.Current()->generation == firstGeneration);

    const IdentitySnapshot error{IdentityReadStatus::ChangedDuringRead, 0, {}};
    events = tracker.Update(error);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Suspended);
    assert(!tracker.HasVerifiedIdentityPair() && tracker.Update(error).empty());
    events = tracker.Update(online);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Resumed);
    assert(events[0].session.generation == firstGeneration);

    auto duplicate = online;
    duplicate.players.push_back(online.players[1]);
    events = tracker.Update(duplicate);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Suspended);
    assert(!tracker.HasVerifiedIdentityPair());
    auto other = online;
    other.players[1].ids = {"583729164028375691", "8pD4sH7zWq2E", "test_q7m4", "8274619503827164953"};
    events = tracker.Update(other);
    assert(events.size() == 2 && events[0].type == IdentitySessionEventType::Ended && events[1].type == IdentitySessionEventType::Started);
    assert(events[1].session.generation > firstGeneration && events[1].session.remoteIds[2] == "test_q7m4");
    const auto nextGeneration = events[1].session.generation;

    auto incomplete = other;
    incomplete.players[1].ids = {};
    events = tracker.Update(incomplete);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Suspended);
    events = tracker.Update(menu);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Ended);
    assert(events[0].session.remoteIds[2] == "test_q7m4" && !tracker.Current());
    menu.players.push_back(PlayerIdentity{});
    assert(tracker.Update(menu).empty() && !tracker.HasVerifiedIdentityPair());
    events = tracker.Update(online);
    assert(events.size() == 1 && events[0].session.generation > nextGeneration);
    online.players[1].ids = online.players[0].ids;
    events = tracker.Update(online);
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Suspended);
    events = tracker.Update({IdentityReadStatus::Ok, Fixture::Root, {}});
    assert(events.size() == 1 && events[0].type == IdentitySessionEventType::Ended && !tracker.Current());
}
