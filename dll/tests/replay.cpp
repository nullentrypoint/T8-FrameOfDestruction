// Headless replay of the production reader, snapshots and GameLog.
// Rendering/logging endpoints are stubs; no process attachment or DLL loading.
#include "stdafx.h"
#include "TekkenBot/GameLog.h"
#include <fstream>
#include <cassert>

static int current_frame;
namespace console {
    std::shared_ptr<PlayerSnapshot> p1, p2;
    void AddLog(const std::string&) {}
    void AddLogError(const std::string& text) { throw std::runtime_error(text); }
}
namespace overlay {
    static uint64_t next_id = 0;
    static std::map<uint64_t, std::string> texts;
    uint64_t AddFlyingText(const std::string& text, const std::optional<ScreenAnchor>&, const std::string&,
        const std::optional<WorldPoint>&) {
        texts[++next_id] = text;
        std::cout << current_frame << '\t' << text << '\n';
        return next_id;
    }
    void UpdateFlyingText(uint64_t id, const std::string& text) {
        assert(texts.count(id) == 1);
        texts[id] = text;
        std::cout << current_frame << '\t' << text << '\n';
    }
}

static void test_persistence(const std::shared_ptr<PlayerSnapshot>& template_player) {
    char directory[MAX_PATH], path[MAX_PATH];
    assert(GetTempPathA(MAX_PATH, directory));
    assert(GetTempFileNameA(directory, "t8f", 0, path));
    auto player = std::make_shared<PlayerSnapshot>(*template_player);
    player->char_id = static_cast<MoveInfoEnums::CharacterCodes>(17);
    player->move_id = 1717;
    player->recovery = 49;
    player->actionable_frame = -1;
    GameLog writer;
    writer.set_action_frame_file(path);
    writer.track_action_frame(true, player);
    player->move_id = 1719;
    player->recovery = 55;
    player->actionable_frame = 26;
    writer.track_action_frame(true, player);
    GameLog reader;
    reader.set_action_frame_file(path);
    assert(reader.get_action_frame(player) == 26);
    player->actionable_frame = 25;
    writer.track_action_frame(true, player);
    assert(ActionFrameStore::load(path).at({17, 1719}) == 25);
    // Same format as Python json.dump; reject a truncated file as a whole.
    { std::ofstream output(path); output << "{\"6:1889\": 26, \"17:1719\": 25}\n"; }
    assert(ActionFrameStore::load(path).size() == 2);
    { std::ofstream output(path); output << "{\"6:1889\": 26,"; }
    assert(ActionFrameStore::load(path).empty());
    assert(DeleteFileA(path));
    assert(ActionFrameStore::load(path).empty());
}

int main(int argc, char** argv) {
    assert(MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(
        static_cast<MoveInfoEnums::AttackType>(6291460)) == "S.LOW");
    assert(MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(
        static_cast<MoveInfoEnums::AttackType>(1048579)) == "S.MID");
    if (argc < 2) return 2;
    assert(GameReader::get_4_bytes_from_data_block({ 0xf7, 0xff, 0xff, 0xff }, 0) == -9);
    assert(GameReader::get_4_bytes_from_data_block({ 0xff, 0xff, 0xff, 0xff }, 0) == -1);
    std::ifstream input(argv[1]);
    size_t count;
    if (!(input >> count) || count > 100) return 2;
    std::vector<std::string> names(count);
    for (auto& name : names) input >> name;
    GameLog log;
    bool tested_persistence = false;
    while (input >> current_frame) {
        std::shared_ptr<PlayerSnapshot> players[2];
        for (auto& player : players) {
            std::map<std::string, int> fields;
            for (const auto& name : names) {
                uint32_t value;
                if (!(input >> value)) return 3;
                std::vector<uint8_t> bytes(4);
                for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(value >> (8 * i));
                fields[name] = GameReader::get_4_bytes_from_data_block(bytes, 0);
            }
            player = std::make_shared<PlayerSnapshot>(fields);
        }
        if (!tested_persistence) {
            test_persistence(players[0]);
            tested_persistence = true;
        }
        if (argc > 2) std::swap(players[0], players[1]);
        try {
            log.track_gamedata(std::make_shared<GameSnapshot>(players[0], players[1], current_frame, false));
        }
        catch (const std::exception& error) {
            std::cerr << "frame " << current_frame << ": " << error.what() << '\n';
            return 4;
        }
    }
    return 0;
}
