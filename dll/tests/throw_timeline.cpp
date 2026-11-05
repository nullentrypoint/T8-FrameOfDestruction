#include "TekkenBot/ThrowTimeline.h"
#include <cassert>
#include <fstream>
#include <cstring>

using namespace MoveInfoEnums;

static std::shared_ptr<PlayerSnapshot> player() {
    auto p = std::make_shared<PlayerSnapshot>(std::map<std::string, int>{
        {"move_id", 1}, {"simple_move_state", 0}, {"attack_type", 0},
        {"attack_startup", 0}, {"complex_move_state", 0}, {"damage_taken", 0},
        {"move_timer", 0}, {"recovery", 0}, {"char_id", 0}, {"throw_tech", 0},
        {"input_direction", 0}, {"input_attack", 0}, {"stun_type", 0}, {"hit_outcome", 0}
    });
    p->throw_fields_valid = true;
    return p;
}

// Text fixtures are converted from the original CE captures by the companion
// script. Both physical player orientations pass through the production tracker.
static void replay(const char* path, bool swap) {
    std::ifstream input(path);
    assert(input);
    ThrowTimelineTracker tracker;
    int frame, index = 0;
    while (input >> frame) {
        auto a = player(), b = player();
        for (auto p : {a, b}) {
            int character, count;
            input >> character >> p->move_id >> p->startup >> p->damage_taken >> p->raw_move_timer
                >> p->throw_animation >> p->raw_throw_tag >> p->raw_throw_input
                >> p->throw_accepted >> p->throw_counter >> p->ground_buffer >> count;
            p->ground_buffer_valid = true;
            p->char_id = static_cast<CharacterCodes>(character);
            assert(count >= 0 && count <= 128);
            while (count--) {
                ThrowBreakRule rule;
                input >> rule.buttons >> rule.start >> rule.end >> rule.transition
                    >> rule.target >> rule.counter >> rule.held >> rule.tackle >> rule.ground_selector;
                p->throw_rules.push_back(rule);
            }
        }
        assert(input);
        auto changed = tracker.update(std::make_shared<GameSnapshot>(swap ? b : a, swap ? a : b, frame, false));
        for (int side = 0; side < 2; ++side) if (changed[side] && changed[side]->visible) {
            const auto& v = *changed[side];
            std::cout << index << ' ' << (swap ? 1 - side : side) << ' ' << v.buttons << ' '
                << v.startup << ' ' << v.window_frames << ' ' << v.input_frame << ' '
                << v.break_frame << ' ' << v.animation_frame << ' ' << static_cast<int>(v.result)
                << ' ' << v.held << ' ' << v.windows.size();
            for (const auto& window : v.windows) std::cout << ' ' << window.start << ' ' << window.end;
            std::cout << ' ' << v.ground << ' ' << static_cast<int>(v.input_origin);
            std::cout << '\n';
        }
        ++index;
    }
}

int main(int argc, char** argv) {
    if (argc > 1) { replay(argv[1], argc > 2); return 0; }
    // Bounded metadata decoding: alternatives, branch selection metadata,
    // held requirements, unknown requirements and failed/truncated reads.
    std::vector<uint8_t> memory(8192);
    auto put = [&](size_t offset, auto value) { std::memcpy(memory.data() + offset, &value, sizeof(value)); };
    put(0x198, uint64_t(0x200));
    for (int i = 0; i < 3; ++i) {
        size_t cancel = 0x200 + i * 0x28, req = 0x400 + i * 0x80;
        put(cancel + 8, uint64_t(req));
        put(cancel + 0x18, 1); put(cancel + 0x1c, 15); put(cancel + 0x20, 20);
        put(cancel + 0x24, uint16_t(99));
        put(req, uint32_t(i == 2 ? 0x87 : 0x83 + i));
        put(req + 4, uint32_t(i == 2 ? 5 : 0));
        put(req + 0x14, uint32_t(0x44c));
    }
    put(0x200 + 3 * 0x28, uint64_t(0x8000));
    auto read = [&](uintptr_t address, void* out, size_t size) {
        if (address > memory.size() || size > memory.size() - address) return false;
        std::memcpy(out, memory.data() + address, size); return true;
    };
    auto rules = ReadThrowRules(0x100, read);
    assert(rules.size() == 3 && rules[0].buttons == 1 && rules[1].buttons == 2 && rules[2].held);
    put(0x504, uint32_t(3));
    put(0x250, uint64_t(0x4000000300000000ULL));
    put(0x268, 18); put(0x26c, 19);
    rules = ReadThrowRules(0x100, read);
    assert(rules.size() == 3 && rules[2].tackle && rules[2].buttons == 4 &&
        !rules[2].held && rules[2].start == 18 && rules[2].end == 19);
    put(0x250, uint64_t(0));
    put(0x268, 1); put(0x26c, 1); put(0x270, 1);
    put(0x500, uint32_t(0x1af)); put(0x504, uint32_t(3));
    put(0x514, uint32_t(0x840d)); put(0x518, uint32_t(1));
    put(0x528, uint32_t(0x44c));
    rules = ReadThrowRules(0x100, read);
    assert(rules.size() == 3 && rules[2].ground_selector == 3 && rules[2].buttons == 4);
    put(0x400, uint32_t(0x999));
    assert(ReadThrowRules(0x100, read).size() == 2);
    put(0x208, uint64_t(0x9000));
    assert(ReadThrowRules(0x100, read).empty());

    for (bool swap : {false, true}) {
        ThrowTimelineTracker tracker;
        auto a = player(), d = player();
        int frame = 0;
        auto step = [&]() {
            auto ac = std::make_shared<PlayerSnapshot>(*a), dc = std::make_shared<PlayerSnapshot>(*d);
            return tracker.update(std::make_shared<GameSnapshot>(swap ? dc : ac, swap ? ac : dc, frame++, false));
        };
        const int side = swap ? 0 : 1;
        a->startup = 12;
        step();
        d->move_id = 2; d->raw_throw_tag = 0xd000001c;
        d->raw_move_timer = 1; d->throw_animation = 1;
        d->throw_rules = {{1, 1, 15, 20, 3, -1, false}, {2, 1, 15, 20, 3, -1, false}};
        auto view = step()[side];
        assert(view && view->buttons == 3 && view->window_frames == -1);
        d->throw_animation += 3121.0f / 4096;
        d->raw_throw_input = 0x400;
        view = step()[side];
        assert(view->window_frames == 20 && view->input_frame == 1);
        d->throw_accepted = true;
        view = step()[side];
        assert(view->break_frame == 2 && view->animation_frame == -1);
        d->move_id = 3;
        view = step()[side];
        assert(view->result == ThrowTimeline::Result::Broken && view->animation_frame == 3);
        assert(!step()[side]);
        // Stale acceptance is not a new break. A tag alone cannot prove success.
        d->move_id = 4;
        view = step()[side];
        assert(view->break_frame == -1);
        d->raw_move_timer = 16;
        view = step()[side];
        assert(view->result == ThrowTimeline::Result::Failed);
        frame += 5;
        view = step()[side];
        assert(view && !view->visible);
        frame = 0;
        view = step()[side];
        assert(view && !view->visible);
    }
    for (bool match : {false, true}) {
        ThrowTimelineTracker tracker;
        auto a = player(), d = player();
        a->ground_buffer_valid = true;
        a->ground_buffer = match ? 1 : 2;
        int frame = 0;
        auto step = [&]() {
            return tracker.update(std::make_shared<GameSnapshot>(std::make_shared<PlayerSnapshot>(*a),
                std::make_shared<PlayerSnapshot>(*d), frame++, false))[1];
        };
        step();
        d->move_id = 2163; d->raw_throw_tag = 0xc000001d;
        d->throw_rules = {{1, 1, 1, 1, 2080, -1, false, false, 1}};
        d->throw_accepted = true;
        auto view = step();
        assert(view && view->ground && view->result == ThrowTimeline::Result::Pending);
        assert(view->window_frames == -1 && view->input_frame == -1);
        d->move_id = 2080; d->throw_rules.clear();
        view = step();
        assert(view->result == (match ? ThrowTimeline::Result::Broken : ThrowTimeline::Result::Unknown));
        if (match) assert(view->input_origin == ThrowTimeline::InputOrigin::Carried);
        // Discontinuities never manufacture a fresh origin from existing input.
        frame += 10; step();
        d->move_id = 1; d->throw_accepted = false; step();
        d->move_id = 2163; d->throw_accepted = true;
        d->throw_rules = {{1, 1, 1, 1, 2080, -1, false, false, 1}};
        view = step();
        if (match) assert(view->input_origin == ThrowTimeline::InputOrigin::Carried);
    }
}
