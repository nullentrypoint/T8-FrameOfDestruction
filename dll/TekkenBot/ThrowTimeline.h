#pragma once
#include "GameSnapshot.h"
#include <array>
#include <cmath>

struct ThrowTimeline {
    enum class Result { Pending, Broken, Failed, Unknown };
    bool visible = false, defender_p1 = false, held = false;
    enum class InputOrigin { None, Buffered, Held, Carried };
    bool ground = false;
    InputOrigin input_origin = InputOrigin::None;
    int buttons = 0;
    int event_frame = -1;
    Result result = Result::Pending;
    int startup = 0, observed_frames = 0;
    int input_frame = -1, break_frame = -1, animation_frame = -1;
    int window_frames = -1;
    struct Window { int start = -1, end = -1; };
    // Separate spans, not a continuous deadline across the tackle's gap.
    std::vector<Window> windows;
};

class ThrowTimelineTracker {
    struct Active {
        ThrowTimeline view;
        std::vector<ThrowBreakRule> rules;
        int start = 0, move = 0, damage = 0;
        float animation = 0;
        bool running = false, closed = false;
        bool ground_match = false, ground_acceptance = false;
        MoveInfoEnums::CharacterCodes attacker{}, defender{};
    };
    std::array<Active, 2> active{};
    struct BufferHistory { int attempt = -1, origin = -1; };
    std::array<BufferHistory, 2> buffers{};
    std::shared_ptr<GameSnapshot> previous;

    static bool tagged(uint32_t tag) {
        return tag == 0xd000001c || tag == 0xe000001f ||
            tag == 0xf000001e || tag == 0x90000018;
    }

public:
    void reset() { active = {}; buffers = {}; previous.reset(); }

    std::array<std::optional<ThrowTimeline>, 2> update(const std::shared_ptr<GameSnapshot>& frame) {
        std::array<std::optional<ThrowTimeline>, 2> changed;
        if (previous && frame->frame_count == previous->frame_count) return changed;
        if (previous && frame->frame_count != previous->frame_count + 1) {
            reset();
            for (auto& value : changed) value = ThrowTimeline{};
        }
        for (int side = 0; side < 2; ++side) {
            const auto& d = side == 0 ? frame->p1 : frame->p2;
            const auto& a = side == 0 ? frame->p2 : frame->p1;
            const auto before = previous ? (side == 0 ? previous->p1 : previous->p2) : nullptr;
            const auto source = previous ? (side == 0 ? previous->p2 : previous->p1) : nullptr;
            auto& s = active[side];
            auto& history = buffers[side];
            bool break_transition = false;
            if (!before || !source) continue;
            if (!d->throw_fields_valid || !a->throw_fields_valid ||
                !before->throw_fields_valid || !source->throw_fields_valid) {
                if (s.running) { s = {}; changed[side] = ThrowTimeline{}; }
                history = {};
                continue;
            }
            const bool same_characters = d->char_id == before->char_id && a->char_id == source->char_id;
            if (!same_characters || !a->ground_buffer_valid || !source->ground_buffer_valid) history = {};
            if (same_characters && d->move_id != before->move_id &&
                std::any_of(d->throw_rules.begin(), d->throw_rules.end(), [](const auto& r) { return r.tackle; }))
                history.attempt = frame->frame_count;
            if (a->ground_buffer_valid && source->ground_buffer_valid && a->ground_buffer != source->ground_buffer) {
                history.origin = -1;
                const int punches = static_cast<int>((d->raw_throw_input & 0x600) >> 9);
                if (same_characters && a->ground_buffer > 0 && a->ground_buffer <= 3 && punches == a->ground_buffer)
                    history.origin = frame->frame_count;
            }
            // Complete the old link before starting a new one, even when both
            // links carry the same tag. Acceptance and animation are distinct.
            if (s.running && (d->move_id != s.move || d->char_id != s.defender || a->char_id != s.attacker)) {
                if (d->char_id == s.defender && a->char_id == s.attacker) {
                    for (const auto& rule : s.rules)
                        if (d->move_id == rule.target) {
                            s.view.animation_frame = frame->frame_count - s.start;
                            break_transition = true;
                            if (s.view.ground && s.ground_match && s.ground_acceptance)
                                s.view.result = ThrowTimeline::Result::Broken;
                        }
                    if (s.view.result == ThrowTimeline::Result::Pending)
                        s.view.result = d->damage_taken > s.damage ? ThrowTimeline::Result::Failed : ThrowTimeline::Result::Unknown;
                } else s.view.result = ThrowTimeline::Result::Unknown;
                s.running = false;
                changed[side] = s.view;
            }
            const bool entered = !break_transition && d->char_id == before->char_id && a->char_id == source->char_id &&
                d->move_id != before->move_id && (tagged(d->raw_throw_tag) ||
                    (a->ground_buffer_valid && std::any_of(d->throw_rules.begin(), d->throw_rules.end(),
                        [](const auto& r) { return r.ground_selector != 0; })));
            if (entered) {
                s = {};
                s.running = true;
                s.start = frame->frame_count;
                s.move = d->move_id;
                s.damage = d->damage_taken;
                s.animation = d->throw_animation;
                s.attacker = a->char_id;
                s.defender = d->char_id;
                s.view.visible = true;
                s.view.event_frame = frame->frame_count;
                s.view.defender_p1 = side == 0;
                for (const auto& rule : d->throw_rules)
                    if (rule.counter < 0 || rule.counter == static_cast<int>(a->throw_counter)) s.rules.push_back(rule);
                // Parameter-3 tackle cancels have two disjoint windows. Only
                // accept the verified shape; arbitrary mixed predicates stay unknown.
                bool tackle = s.rules.size() == 2 && s.rules[0].tackle && s.rules[1].tackle &&
                    s.rules[0].start == 1 && s.rules[1].start > s.rules[0].end &&
                    s.rules[0].buttons == s.rules[1].buttons;
                if (tackle) s.view.windows.resize(2);
                // Ordinary alternatives must share a timing domain.
                if (!tackle && !s.rules.empty()) {
                    const auto first = s.rules.front();
                    for (const auto& rule : s.rules)
                        if (rule.tackle || rule.start != first.start || rule.end != first.end || rule.held != first.held) {
                            s.rules.clear(); break;
                        }
                }
                for (const auto& rule : s.rules) s.view.buttons |= rule.buttons;
                s.view.ground = s.rules.size() == 1 && s.rules.front().ground_selector != 0;
                if (s.view.ground) {
                    const int required = s.rules.front().ground_selector;
                    s.ground_match = a->ground_buffer_valid && a->ground_buffer == required;
                    s.ground_acceptance = d->throw_accepted && !before->throw_accepted;
                    if (s.ground_match) {
                        const bool current = history.attempt >= 0 && history.origin >= history.attempt;
                        const int punches = static_cast<int>((d->raw_throw_input & 0x600) >> 9);
                        s.view.input_origin = !current ? ThrowTimeline::InputOrigin::Carried :
                            punches == required ? ThrowTimeline::InputOrigin::Held : ThrowTimeline::InputOrigin::Buffered;
                    }
                }
                const bool chain = !s.rules.empty() && s.rules.front().held;
                s.view.startup = chain || s.view.ground ? 0 : source->startup;
                s.view.held = chain && (d->raw_throw_input & 0x600) == 0x200 &&
                    (before->raw_throw_input & 0x600) == 0x200;
            }
            if (!s.running) continue;
            auto& view = s.view;
            const int elapsed = frame->frame_count - s.start;
            view.observed_frames = elapsed + 1;
            if (!view.ground && !view.held && view.input_frame < 0 &&
                (d->raw_throw_input & ~before->raw_throw_input & 0x600)) view.input_frame = elapsed;
            if (view.ground) {
                // A one-frame conditional evaluation is not a reaction window.
                // Require matching selector + fresh signal + declared target
                // before BROKEN. Other routes (e.g. B3) remain unconfirmed.
                if (s.ground_match && s.ground_acceptance) view.break_frame = 0;
                if (d->damage_taken > s.damage) view.result = ThrowTimeline::Result::Failed;
            }
            else if (!view.windows.empty()) {
                bool inside = false;
                for (size_t i = 0; i < s.rules.size(); ++i) {
                    const auto& rule = s.rules[i];
                    auto& window = view.windows[i];
                    if (d->raw_move_timer >= rule.start && d->raw_move_timer <= rule.end) {
                        if (window.start < 0) window.start = elapsed;
                        inside = true;
                    }
                    if (window.start >= 0 && window.end < 0 && d->raw_move_timer > rule.end)
                        window.end = elapsed;
                }
                // Acceptance belongs to this move and this window, never to a
                // subsequent ground action. Do not fail at the first deadline.
                if (view.break_frame < 0 && inside && d->throw_accepted && !before->throw_accepted) {
                    view.break_frame = elapsed;
                    view.result = ThrowTimeline::Result::Broken;
                }
                if (view.break_frame < 0 && d->raw_move_timer > s.rules.back().end)
                    view.result = ThrowTimeline::Result::Failed;
                if (view.windows.back().end >= 0) {
                    view.window_frames = 0;
                    for (const auto& window : view.windows) {
                        if (window.start < 0 || window.end < 0) { view.window_frames = -1; break; }
                        view.window_frames += window.end - window.start;
                    }
                }
            }
            else if (!s.rules.empty()) {
                const auto& rule = s.rules.front();
                // Derive game-frame duration from the observed animation rate,
                // then replace the estimate with the actual window crossing.
                if (elapsed == 1) {
                    const float rate = d->throw_animation - s.animation;
                    if (std::isfinite(rate) && rate > 0.1f && rate <= 2.0f &&
                        std::isfinite(s.animation) && s.animation >= 0 && s.animation <= 1.1f)
                        view.window_frames = static_cast<int>(std::ceil((rule.end + 1 - s.animation) / rate));
                }
                if (!s.closed && d->raw_move_timer > rule.end) {
                    s.closed = true;
                    view.window_frames = elapsed;
                }
                if (view.break_frame < 0 && d->throw_accepted && !before->throw_accepted &&
                    !s.closed && d->raw_move_timer >= rule.start) {
                    view.break_frame = elapsed;
                    view.result = ThrowTimeline::Result::Broken;
                }
                if (s.closed && view.break_frame < 0) view.result = ThrowTimeline::Result::Failed;
            }
            if (elapsed > 600) {
                if (view.result == ThrowTimeline::Result::Pending) view.result = ThrowTimeline::Result::Unknown;
                s.running = false;
            }
            changed[side] = view;
        }
        previous = frame;
        return changed;
    }
};
