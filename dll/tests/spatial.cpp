// Headless origin/projection checks. Optional argument: the per-frame spatial
// capture described in README.md; no attachment to a running game is needed.
#include "stdafx.h"
#include "TekkenBot/GameLog.h"
#include <cassert>
#include <fstream>
#include <limits>

namespace console {
    std::shared_ptr<PlayerSnapshot> p1, p2;
    void AddLog(const std::string&) {}
    void AddLogError(const std::string& text) { throw std::runtime_error(text); }
}
namespace overlay {
    static uint64_t count = 0;
    uint64_t AddFlyingText(const std::string&, const std::optional<ScreenAnchor>&, const std::string&,
        const std::optional<WorldPoint>&) { return ++count; }
    void UpdateFlyingText(uint64_t, const std::string&) {}
}

static std::shared_ptr<PlayerSnapshot> player() {
    return std::make_shared<PlayerSnapshot>(std::map<std::string, int>{
        {"move_id", 32769}, {"simple_move_state", 3}, {"attack_type", 0},
        {"attack_startup", 0}, {"complex_move_state", 0}, {"damage_taken", 0},
        {"move_timer", 0}, {"recovery", 0}, {"char_id", 6}, {"throw_tech", 0},
        {"input_direction", 0x20}, {"input_attack", 0}, {"stun_type", 0}, {"hit_outcome", 0}
    });
}

static void test_origins(bool swap) {
    GameLog log;
    const WorldPoint old{1000, 1400, 2000}, contact{1200, 500, 2100}, whiff{1800, 1400, 2300};
    auto push = [&](int frame, int outcome, WorldPoint point, int timer = 0) {
        auto attacker = player(), receiver = player();
        receiver->contact_position = point;
        receiver->hit_outcome = static_cast<MoveInfoEnums::HitOutcome>(outcome);
        receiver->raw_move_timer = frame;
        attacker->attack_hitbox_position = whiff;
        if (timer) {
            attacker->startup = 10;
            attacker->raw_move_timer = attacker->move_timer = timer;
        }
        auto snapshot = std::make_shared<GameSnapshot>(
            swap ? receiver : attacker, swap ? attacker : receiver, frame, false);
        log.track_gamedata(snapshot);
        return attacker;
    };
    push(1, 0, old);
    push(2, 12, contact, 11);
    assert(log.get_flying_origin(!swap) == contact); // contact wins over hitbox
    push(3, 12, contact);
    assert(log.get_flying_origin(!swap) == contact); // deferred block/stance entry
    for (int i = 4; i <= 10; ++i) push(i, 0, contact);
    assert(!log.get_flying_origin(!swap)); // idle vectors are stale
    push(11, 0, contact, 11);
    assert(log.get_flying_origin(!swap) == whiff); // not the previous hit point
    auto projectile = push(12, 0, contact);
    push(13, 12, old);
    for (int i = 14; i <= 20; ++i) push(i, 12, old);
    assert(!log.get_flying_origin(!swap));
    assert(log.get_flying_origin(!swap, projectile) == old); // delayed contact
    push(0, 0, contact); // practice reset discards history
    assert(!log.get_flying_origin(!swap));
    push(1, 0, contact, 9);
    assert(!log.get_flying_origin(!swap)); // startup is not an active hitbox
}

static void test_projection() {
    CameraSnapshot camera;
    // Camera looking along Unreal +X, +Z up, horizontal FOV 90, aspect 16:9.
    camera.view[2] = camera.view[4] = camera.view[9] = camera.view[15] = 1;
    camera.projection[0] = camera.projection[11] = 1;
    camera.projection[5] = 16.0 / 9.0;
    camera.projection[14] = 10;
    auto center = camera.project({0, 0, 1000});
    assert(center && std::abs(center->horizontal) < 1e-6 && std::abs(center->vertical) < 1e-6);
    auto right = camera.project({-1000, 0, 1000});
    auto up = camera.project({0, 1000, 1000});
    assert(right && std::abs(right->horizontal - 1) < 1e-6);
    assert(up && std::abs(up->vertical - 16.0f / 9.0f) < 1e-6);
    assert(!camera.project({0, 0, -1000})); // behind camera
    camera.view[2] = camera.view[4] = 0;
    camera.view[0] = -1;
    camera.view[6] = 1; // yaw 90: camera looks along Unreal +Y
    center = camera.project({-1000, 0, 0});
    assert(center && std::abs(center->horizontal) < 1e-6);
    camera.view[14] = -100; // camera translated by +100 in its forward direction
    center = camera.project({-2000, 0, 0});
    assert(center && std::abs(center->horizontal) < 1e-6 && std::abs(center->vertical) < 1e-6);
    camera.projection[0] = std::numeric_limits<double>::quiet_NaN();
    assert(!camera.project({0, 1000, 0}));
    assert(!GameReader::decode_position(std::vector<uint8_t>(11), 0));
    assert(!GameReader::decode_position(std::vector<uint8_t>(12), 0));
    assert(!GameReader::decode_position(std::vector<uint8_t>(12), SIZE_MAX));
}

static void replay_capture(const char* path, bool swap) {
    ConfigParser config;
    assert(config.load("../config/memory_address.ini"));
    const auto& fields = config.data().at("PlayerDataAddress");
    const auto& spatial = config.data().at("SpatialPlayerData");
    std::ifstream input(path, std::ios::binary);
    assert(input);
    GameLog log;
    int frame;
    uint8_t camera_data[64];
    std::vector<uint8_t> data(0x1b900);
    int count = 0, hits = 0, previous_damage = -1;
    const auto before = overlay::count;
    while (input.read(reinterpret_cast<char*>(&frame), sizeof(frame))) {
        assert(input.read(reinterpret_cast<char*>(camera_data), sizeof(camera_data)));
        assert(input.read(reinterpret_cast<char*>(data.data()), data.size()));
        std::shared_ptr<PlayerSnapshot> players[2];
        for (int side = 0; side < 2; ++side) {
            const int offset = side * 0xdc80;
            std::map<std::string, int> values;
            for (const auto& [name, offsets] : fields) {
                values[name] = GameReader::get_4_bytes_from_data_block(data, offset + static_cast<int>(offsets[0]));
            }
            players[side] = std::make_shared<PlayerSnapshot>(values);
            players[side]->contact_position = GameReader::decode_position(data,
                offset + spatial.at("contact_position")[0]);
            players[side]->attack_hitbox_position = GameReader::decode_position(data,
                offset + spatial.at("attack_hitbox_position")[0]);
        }
        auto snapshot = std::make_shared<GameSnapshot>(players[swap ? 1 : 0], players[swap ? 0 : 1], frame, false);
        log.track_gamedata(snapshot);
        if (previous_damage >= 0 && previous_damage != players[0]->damage_taken) {
            ++hits;
            assert(log.get_flying_origin(swap) == players[0]->contact_position);
            assert(players[0]->contact_position);
        }
        previous_damage = players[0]->damage_taken;
        ++count;
    }
    assert(count == 1500 && hits == 8 && overlay::count - before == 8);
    std::cout << "capture " << (swap ? "swapped" : "normal") << ": 1500 frames, 8 contacts, 8 labels OK\n";
}

int main(int argc, char** argv) {
    test_projection();
    test_origins(false);
    test_origins(true);
    if (argc > 1) {
        replay_capture(argv[1], false);
        replay_capture(argv[1], true);
    }
    std::cout << "Spatial origin/projection checks passed\n";
}
