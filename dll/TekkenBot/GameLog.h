#pragma once

#include <vector>
#include <map>
#include <string>
#include <memory>
#include "GameSnapshot.h"
#include "GameReader.h"
#include "MoveInfoEnums.h"
#include "../namespaces.h"

struct Entry {
    int char_id;
    std::string char_name;
    bool is_player;
    std::string move_id;
    std::string fa;
    std::string hit_type;
    std::string complex_state;
};

class GameLog {
private:
    void* obj;  // Equivalent to Python's typing.Any
    std::vector<std::shared_ptr<GameSnapshot>> state_log;

public:
    GameLog() : obj(nullptr) {}

    std::shared_ptr<PlayerSnapshot> get(bool is_p1, int frames_ago = 0) {
        if (state_log.size() <= frames_ago) {
            return nullptr;
        }
        auto state = state_log[state_log.size() - 1 - frames_ago];
        return is_p1 ? state->p1 : state->p2;
    }

    void update(std::shared_ptr<GameReader> game_reader) {
        //auto overlay = TekkenBot420::getInstance()->overlay;
        //overlay->update_location(game_reader);
        auto game_snapshot = game_reader->get_updated_state(0);

        if (game_snapshot != nullptr) {
            if (state_log.empty() || game_snapshot->frame_count != state_log.back()->frame_count) {
                if (!state_log.empty()) {
                    int frames_lost = game_snapshot->frame_count - state_log.back()->frame_count - 1;
                    int missed_states = std::min(7, frames_lost);

                    for (int i = 0; i < missed_states; i++) {
                        auto dropped_state = game_reader->get_updated_state(missed_states - i);
                        if (dropped_state != nullptr) {
                            track_gamedata(dropped_state);
                        }
                    }
                }
                track_gamedata(game_snapshot);
            }
        }
    }

    void track_gamedata(std::shared_ptr<GameSnapshot> game_snapshot) {
        //auto overlay = TekkenBot420::getInstance()->overlay;
        if (!state_log.empty() && state_log.back()->frame_count == game_snapshot->frame_count) {
            return;
        }

        state_log.push_back(game_snapshot);

        void* debug_obj = nullptr;  // For debugging
        if (debug_obj != obj) {
            //std::cout << game_snapshot->frame_count << " " << debug_obj << std::endl;
            obj = debug_obj;
        }

        //overlay->update_state(shared_from_this());
        //Record::record_if_activated();
        update_state();

        if (state_log.size() > 3000) {
            state_log.erase(state_log.begin());
        }
    }

    void update_state() {
        read_player_state(true);
        read_player_state(false);
    }

    void read_player_state(bool is_p1) {
        Entry entry;
        if (is_starting_attack(is_p1)) {
            entry = build(is_p1);
        }
        else {
            auto throw_break_string = get_throw_break(is_p1);
            if (throw_break_string.length() != 0) {
                entry.move_id = throw_break_string;
            }
            else
            {
                return;
            }
        }

        if (entry.fa.length() != 0 && entry.fa != "KND" && entry.fa != "JGL") {
            auto val = kv::movesOverrides->GetValue(entry.move_id, entry.char_name + "." + entry.complex_state);
            console::AddLog(std::format("{} char: {}({}) move_id:{} hit_type:{} complex_state:{} fa:{} override: {}",
                (entry.is_player ? "p1: " : "p2: "),
                entry.char_name,
                entry.char_id,
                entry.move_id,
                entry.hit_type,
                entry.complex_state,
                entry.fa,
                val
            ));
            if (val != "") {
                entry.fa = val;
            }
            overlay::AddFlyingText(entry.fa);
        }

        try {
            console::p1 = get(true, 0);
            console::p2 = get(false, 0);
        }
        catch (...) {
            console::AddLogError("exception");
        }
    }

    Entry build(bool is_p1) {
        Entry entry;
        auto attacker = get(is_p1, 1);
        auto receiver = get(!is_p1, 0);

        entry.char_id = int(attacker->char_id);
        entry.char_name = MoveInfoEnums::SafeEnum<MoveInfoEnums::CharacterCodes>::to_string(attacker->char_id);
        entry.is_player = is_p1;
        entry.move_id = std::to_string(attacker->move_id);
        entry.fa = get_fa(is_p1, attacker, receiver);
        entry.complex_state = MoveInfoEnums::SafeEnum<MoveInfoEnums::ComplexMoveStates>::to_string(receiver->complex_state);
        entry.hit_type = MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(attacker->attack_type);

        return entry;
    }

    std::string get_fa(bool is_p1,
        std::shared_ptr<PlayerSnapshot> attacker,
        std::shared_ptr<PlayerSnapshot> receiver) {
        if (receiver->simple_state == MoveInfoEnums::SimpleMoveStates::KNOCKDOWN) {
            return "KND";
        }
        else if (receiver->hit_outcome == MoveInfoEnums::HitOutcome::JUGGLE) {
            return "JGL";
        }
        else {
            int time_till_recovery_p1 = attacker->frames_til_next_move;
            int time_till_recovery_p2 = (receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE)
                ? 0
                : receiver->frames_til_next_move;

            int raw_fa = time_till_recovery_p2 - time_till_recovery_p1;
            std::string raw_fa_str = std::to_string(raw_fa);

            if (raw_fa > 0) {
                raw_fa_str = "+" + raw_fa_str;
            }
            return raw_fa_str;
        }
    }
    

    bool is_starting_attack(bool is_p1) {
        int before = 2;
        auto player = get(is_p1, before);
        if (player != nullptr && player->startup != 0) {
            int cur_frame_count = state_log.back()->frame_count;
            int prev_frame_count = state_log[state_log.size() - 2]->frame_count;
            int dropped_frames = cur_frame_count - prev_frame_count - 1;
            int diff = player->startup - player->move_timer - 1;
            if (diff >= 0 && diff <= dropped_frames) {
                auto previous_player = get(is_p1, before + 1);
                if (previous_player != nullptr && previous_player->move_timer < player->move_timer) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string get_throw_break(bool is_p1) {
        auto state = get(!is_p1);
        auto throw_tech = state->throw_tech;
        if (throw_tech == MoveInfoEnums::ThrowTechs::NONE ||
            throw_tech == MoveInfoEnums::ThrowTechs::BROKEN_ThrowTechs) {
            return "";
        }

        auto prev_state = get(is_p1, 2);
        if (prev_state == nullptr) {
            return "";
        }
        int move_id = prev_state->move_id;
        std::string current_buttons = MoveInfoEnums::SafeEnum<MoveInfoEnums::InputAttackCodes>::to_string(get(!is_p1, 1)->input_button);
        if (current_buttons.find('1') == std::string::npos &&
            current_buttons.find('2') == std::string::npos) {
            if (move_id != get(is_p1, 1)->move_id) {
                return "br: " + MoveInfoEnums::SafeEnum<MoveInfoEnums::ThrowTechs>::to_string(throw_tech);
            }
            return "";
        }

        std::string correct = MoveInfoEnums::SafeEnum<MoveInfoEnums::ThrowTechs>::to_string(state->throw_tech);

        int i = 2;
        for (int _ = 0; _ < 1000; _++) {
            state = get(!is_p1, i);
            if (state == nullptr || move_id != get(is_p1, i)->move_id) {
                std::string relevant = current_buttons;
                // Replace operations would be implemented here
                auto throw_break = MoveInfoEnums::SafeEnum<MoveInfoEnums::InputAttackCodes>::from_string(relevant);
                std::string break_string = relevant;
                // Replace operations would be implemented here
                return break_string + "/" + correct + " " + std::to_string(i - 2);
            }
            std::string buttons = MoveInfoEnums::SafeEnum<MoveInfoEnums::InputAttackCodes>::to_string(state->input_button);
            if (buttons.find('1') != std::string::npos || buttons.find('2') != std::string::npos) {
                return "";
            }
            i++;
        }
        throw std::runtime_error("get_throw_break");
    }

    bool just_lost_health(bool is_p1) {
        auto prev_state = get(is_p1, 2);
        if (prev_state == nullptr) {
            return false;
        }
        auto next_state = get(is_p1, 1);
        return next_state->damage_taken != prev_state->damage_taken;
    }

    std::string get_interrupt(bool is_p1) {
        auto state = get(!is_p1, 1);
        if (state->move_timer != 0) {
            int d = state->startup - state->move_timer;
            if (d > 0) {
                return "c" + std::to_string(d) + " " + std::to_string(state->startup);
            }
        }
        if (state->complex_state != MoveInfoEnums::ComplexMoveStates::BLOCK) {
            for (int i = 2; i < get(is_p1)->startup; i++) {
                auto s = get(!is_p1, i);
                if (s != nullptr) {
                    if (s->complex_state == MoveInfoEnums::ComplexMoveStates::BLOCK) {
                        auto ss = get(is_p1, i - 1);
                        return "b" + std::to_string(ss->startup - ss->move_timer) + " " +
                            std::to_string(ss->startup) + " " +
                            std::to_string(state_log[state_log.size() - i]->frame_count);
                    }
                }
            }
        }
        return "";
    }
};