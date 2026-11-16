#include "VoiceChat/VoiceParticipantRows.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

int main() {
    using namespace VoiceChat;
    LoungeView lounge;
    lounge.status = LoungeReadStatus::Ok;
    lounge.capturedAt = 1000;
    LoungeMemberView row;
    row.peer.steamId = 76561198000000001ULL;
    row.peer.gameName = "Game name";
    row.peer.name = "Different Steam name";
    lounge.members.push_back(row);
    PeerView battle, local;
    battle.steamId = 76561198000000002ULL;
    local.steamId = 76561198000000003ULL;
    auto rows = VoiceParticipantRows(lounge, battle, local, 1100);
    assert(rows.size() == 1 && rows.front().peer.steamId == row.peer.steamId);
    assert(rows.front().peer.gameName != rows.front().peer.name);
    assert(!rows.front().peer.connected && rows.front().peer.ping == -1);
    assert(VoiceParticipantRows(lounge, battle, local, 2001).empty());
    assert(!FreshLounge(lounge, 999));
    {
        auto custom = lounge;
        custom.customRoom = true;
        custom.members.front().peer.connected = true;
        auto disconnected = custom.members.front(); disconnected.peer.steamId++;
        disconnected.peer.connected = false; custom.members.push_back(disconnected);
        assert(CustomRoomHudRows(custom, 1100).size() == 1);
        assert(CustomRoomHudRows(custom, 2001).empty());
        auto spectator = custom.members.front(); spectator.peer.steamId += 2;
        custom.members.push_back(spectator);
        custom.members[1].peer.connected = true;
        const MatchVoicePair fighters{custom.members[0].peer.steamId, custom.members[1].peer.steamId, 1000};
        const auto fighterRows = CustomRoomHudRows(custom, 1100, fighters);
        assert(fighterRows.size() == 2);
        for (const auto& fighter : fighterRows) assert(fighter.peer.steamId != spectator.peer.steamId);
        assert(VoiceParticipantRows(custom, battle, local, 1100).size() == 3); // Spectator remains in menu.
        for (size_t count : {size_t{1}, size_t{8}, size_t{64}}) {
            float previousX = 1280;
            for (size_t i = 0; i < count; ++i) {
                const auto tile = CustomRoomHudTile(1280, 720, count, i);
                assert(tile.x >= 0 && tile.x + tile.size <= 1280 && tile.y >= 0 && tile.y + tile.size <= 720);
                assert(tile.x + tile.size < previousX);
                previousX = tile.x;
            }
        }
    }
    {
        LoungeView roster = lounge;
        roster.members.clear();
        const auto add = [&](uint64_t id, const char* name, bool connected, bool self = false) {
            LoungeMemberView item;
            item.peer.steamId = id; item.peer.gameName = name;
            item.peer.name = "Unrelated Steam name";
            item.peer.connected = connected; item.self = self;
            roster.members.push_back(item);
        };
        add(10, "Beta", false);
        add(20, "zebra", true);
        add(0, "Console should be omitted", true);
        add(30, "alpha", false);
        add(40, "ALPHA", true);
        add(0, "You", false, true); // Self remains first even before Steam binds.
        add(50, "", false);
        const auto sorted = VoiceParticipantRows(roster, battle, local, 1100);
        assert(sorted.size() == 6 && sorted[0].self);
        assert(sorted[1].peer.steamId == 40 && sorted[2].peer.steamId == 20);
        assert(sorted[3].peer.steamId == 30 && sorted[4].peer.steamId == 10);
        assert(sorted[5].peer.steamId == 50);
        assert(CompareVoiceGameNames("alpha", "ALPHA") == 0);
        assert(CompareVoiceGameNames("\xD0\x90\xD0\xBD\xD0\xBD\xD0\xB0", "\xD0\x91\xD0\xBE\xD1\x80\xD0\xB8\xD1\x81") < 0);
    }
    lounge.status = LoungeReadStatus::NotInLounge;
    rows = VoiceParticipantRows(lounge, battle, local, 1100);
    assert(rows.size() == 2 && rows.front().self && rows.back().peer.steamId == battle.steamId);
    battle.level = .8f; battle.levelAt = 1000;
    assert(FreshVoiceLevel(battle, 1100) == 0); // Disconnected signal is never displayed.
    battle.connected = true;
    assert(FreshVoiceLevel(battle, 1100) == .8f);
    assert(FreshVoiceLevel(battle, 1250) == 0);
    battle.level = std::numeric_limits<float>::quiet_NaN();
    assert(FreshVoiceLevel(battle, 1100) == 0);

    LoungeCamera camera;
    camera.view[0] = camera.view[5] = camera.view[10] = camera.view[15] = 1;
    camera.projection[0] = camera.projection[5] = camera.projection[10] = camera.projection[11] = 1;
    auto screen = camera.Project({5, 0, 10});
    assert(screen && screen->x == .5f && screen->y == 0);
    assert(std::abs(screen->cameraDistance - std::sqrt(125.0)) < 1e-9);
    assert(LoungeMarkerInRange(*screen));
    const auto distant = camera.Project({1200, 0, 1700});
    assert(distant && !LoungeMarkerInRange(*distant)); // Radial distance, not just camera-space depth.
    const auto boundary = camera.Project({0, 0, LoungeAvatarMaxDistance});
    assert(boundary && LoungeMarkerInRange(*boundary));
    int distanceSetting = 2000;
    assert(ParseLoungeAvatarDistance("3500", distanceSetting) && distanceSetting == 3500);
    loungeAvatarDistance = distanceSetting;
    assert(LoungeMarkerInRange(*distant));
    assert(ParseLoungeAvatarDistance("0", distanceSetting));
    loungeAvatarDistance = distanceSetting;
    assert(!LoungeMarkerInRange(*screen));
    for (const auto text : {"-1", "nan", "2000oops", "3.5", "100001", "99999999999999999", ""})
        assert(!ParseLoungeAvatarDistance(text, distanceSetting) && distanceSetting == 0);
    loungeAvatarDistance = static_cast<int>(LoungeAvatarMaxDistance);
    assert(!camera.Project({0, 0, -10}));
    assert(!camera.Project({50, 0, 10}));
    assert(!camera.Project({0, std::numeric_limits<double>::infinity(), 10}));
    camera.view[12] = -5; // Camera translation must affect projection.
    screen = camera.Project({5, 0, 10});
    assert(screen && screen->x == 0);
    camera.projection[11] = 0;
    assert(!camera.Project({0, 0, 10}));

    struct Memory {
        uintptr_t world = 0x20000, state = 0x30000;
        int worldReads = 0;
        bool change = false;
        LoungeCamera camera;
    } memory;
    const auto read = [](void* context, uintptr_t address, void* output, size_t size) -> bool {
        auto& m = *static_cast<Memory*>(context);
        if (address == 0x10100 && size == 8) {
            auto world = m.world + (m.change && ++m.worldReads > 1 ? 0x1000 : 0);
            memcpy(output, &world, size); return true;
        }
        if (address == 0x20020 && size == 8) { memcpy(output, &m.state, size); return true; }
        if (address == 0x30100 && size == 128) { memcpy(output, m.camera.view.data(), size); return true; }
        if (address == 0x30200 && size == 128) { memcpy(output, m.camera.projection.data(), size); return true; }
        return false;
    };
    LoungeCameraLayout layout{{0x100, 0x20}, 0x100, 0x200};
    assert(ReadLoungeCamera(0x10000, 0x20000, layout, read, &memory));
    assert(!ReadLoungeCamera(0x10000, 0x21000, layout, read, &memory));
    memory.change = true;
    assert(!ReadLoungeCamera(0x10000, 0x20000, layout, read, &memory));
    std::cout << "Voice participant state, stale signals and lounge projection checks passed\n";
}
