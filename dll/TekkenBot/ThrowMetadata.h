#pragma once
#include <cstdint>
#include <vector>

struct ThrowBreakRule {
    int buttons = 0; // Alternatives: bit 0 = 1, bit 1 = 2, bit 2 = 1+2.
    int start = 0, end = 0, transition = 0, target = 0;
    int counter = -1;
    bool held = false;
    bool tackle = false;
    int ground_selector = 0; // 1AF:N,840D:1; compares OTHER player's +1564.
};

// Only decode verified requirements. Reads and list walks are bounded, and an
// incomplete list is discarded rather than publishing a partial requirement.
template<class Read>
std::vector<ThrowBreakRule> ReadThrowRules(uintptr_t move, Read read) {
    std::vector<ThrowBreakRule> rules;
    uintptr_t cancels = 0;
    if (!move || !read(move + 0x98, &cancels, sizeof(cancels)) || !cancels) return {};
    for (int i = 0; i < 128; ++i) {
        const auto p = cancels + i * 0x28;
        uint64_t command = 0;
        uintptr_t requirements = 0;
        if (!read(p, &command, 8) || !read(p + 8, &requirements, 8)) return {};
        if (command == 0x8000) return rules;
        if ((command != 0 && command != 0x4000000300000000ULL) || !requirements) continue;
        ThrowBreakRule rule;
        bool supported = true, terminated = false, ground_marker = false;
        for (int j = 0; j < 64; ++j) {
            uint32_t pair[2]{};
            if (!read(requirements + j * 0x14, pair, sizeof(pair))) return {};
            if (pair[0] == 0x44c) { terminated = true; break; }
            switch (pair[0]) {
            case 0x1af:
                if (rule.ground_selector || pair[1] < 1 || pair[1] > 3) supported = false;
                rule.ground_selector = pair[1];
                break;
            case 0x840d:
                if (ground_marker || pair[1] != 1) supported = false;
                ground_marker = true;
                break;
            case 0x1cc: case 0x1cd:
                if (pair[1] || rule.counter != -1) supported = false;
                rule.counter = pair[0] == 0x1cc ? 1 : 0;
                break;
            case 0x83: case 0x84: case 0x85:
                if (pair[1] || rule.buttons) supported = false;
                rule.buttons = 1 << (pair[0] - 0x83);
                break;
            case 0x87:
                if ((pair[1] != 5 && pair[1] != 3) || rule.buttons) supported = false;
                rule.buttons = pair[1] == 3 ? 4 : 1;
                rule.held = pair[1] == 5;
                rule.tackle = pair[1] == 3;
                break;
            default: supported = false; break;
            }
        }
        if (!terminated) return {};
        if (rule.ground_selector || ground_marker) {
            if (!rule.ground_selector || !ground_marker || rule.buttons || rule.counter != -1 || command != 0)
                supported = false;
            else rule.buttons = 1 << (rule.ground_selector - 1);
        }
        if (!supported || !rule.buttons) continue;
        if (command != 0 && !rule.tackle) continue;
        uint16_t target = 0;
        if (!read(p + 0x18, &rule.start, 4) || !read(p + 0x1c, &rule.end, 4) ||
            !read(p + 0x20, &rule.transition, 4) || !read(p + 0x24, &target, 2)) return {};
        rule.target = target;
        if (rule.ground_selector && (rule.start != 1 || rule.end != 1 || rule.transition != 1)) continue;
        if (rule.start < 1 || (rule.start != 1 && !rule.tackle) || rule.end < rule.start || rule.end > 120 ||
            rule.transition < 1 || rule.transition > 600) continue;
        rules.push_back(rule);
    }
    return {};
}
