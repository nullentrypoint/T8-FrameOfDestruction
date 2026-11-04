#pragma once

#include <vector>
#include <map>
#include <string>
#include <memory>
#include <optional>
#include "GameSnapshot.h"
#include "ActionFrameStore.h"
#include "GameReader.h"
#include "MoveInfoEnums.h"
#include "../namespaces.h"

class GameLog {
private:
    void* obj;  // Equivalent to Python's typing.Any
    std::vector<std::shared_ptr<GameSnapshot>> state_log;
    std::map<bool, int> pending;
    std::map<bool, std::shared_ptr<PlayerSnapshot>> pending_source;
    using MoveKey = std::pair<int, int>;
    std::map<MoveKey, int> action_frames;
    std::string action_frame_file;
    bool action_frame_file_initialized = false;
    std::map<bool, std::pair<MoveKey, int>> last_action_field;
    struct PendingHeat {
        std::shared_ptr<GameSnapshot> started;
        Entry entry;
    };
    std::map<bool, PendingHeat> pending_heat;
    std::map<bool, PendingHeat> pending_break;
    struct PendingProjectile {
        std::shared_ptr<GameSnapshot> started;
        std::shared_ptr<PlayerSnapshot> source;
    };
    std::map<bool, PendingProjectile> pending_projectile;
    struct RecentEntry {
        std::shared_ptr<GameSnapshot> snapshot;
        Entry entry;
        uint64_t text_id;
    };
    std::map<bool, RecentEntry> recent_entries;
    struct RecentPublication {
        RecentEntry displayed;
        std::shared_ptr<PlayerSnapshot> previous;
    };
    std::map<bool, RecentPublication> recent_publications;
    std::map<bool, std::pair<int, int>> finished_attacks;

public:
    GameLog() : obj(nullptr) {}

    void set_action_frame_file(const std::string& path) {
        action_frame_file = path;
        action_frame_file_initialized = true;
        if (!path.empty()) {
            for (const auto& [key, frame] : ActionFrameStore::load(path)) {
                action_frames.try_emplace(key, frame);
            }
        }
    }

    std::shared_ptr<PlayerSnapshot> get(bool is_p1, int frames_ago = 0) {
        if (state_log.size() <= frames_ago) {
            return nullptr;
        }
        auto state = state_log[state_log.size() - 1 - frames_ago];
        return is_p1 ? state->p1 : state->p2;
    }

    void update(std::shared_ptr<GameReader> game_reader) {
        if (!action_frame_file_initialized) {
            set_action_frame_file(game_reader->get_action_frame_path());
        }
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

        if (!state_log.empty() && game_snapshot->frame_count < state_log.back()->frame_count) {
            state_log.clear();
            pending.clear();
            pending_source.clear();
            pending_heat.clear();
            pending_break.clear();
            pending_projectile.clear();
            recent_entries.clear();
            recent_publications.clear();
            finished_attacks.clear();
            last_action_field.clear();
        }
        state_log.push_back(game_snapshot);
        track_action_frame(true, game_snapshot->p1);
        track_action_frame(false, game_snapshot->p2);

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

    void refresh_late_stance(bool is_p1) {
        auto recent = recent_entries.find(is_p1);
        if (recent == recent_entries.end()) return;
        const auto& snapshot = recent->second.snapshot;
        auto source = is_p1 ? snapshot->p1 : snapshot->p2;
        auto target = is_p1 ? snapshot->p2 : snapshot->p1;
        auto attacker = get(is_p1);
        auto receiver = get(!is_p1);
        int age = state_log.back()->frame_count - snapshot->frame_count;
        if (age < 0 || age > 60 || attacker->char_id != source->char_id ||
            receiver->char_id != target->char_id || receiver->move_id != target->move_id ||
            receiver->raw_move_timer < target->raw_move_timer) {
            recent_entries.erase(recent);
            return;
        }
        if (attacker->move_id == source->move_id || !get_action_frame(attacker)) return;
        auto transition = find_stance_attack(is_p1, attacker, 0);
        if (!transition || transition->first->move_id != source->move_id) return;
        auto& entry = recent->second.entry;
        entry.fa = get_fa(is_p1, attacker, receiver);
        auto publication = recent_publications.find(is_p1);
        if (publication != recent_publications.end() &&
            publication->second.displayed.text_id == recent->second.text_id) {
            publication->second.displayed.entry.fa = entry.fa;
        }
        overlay::UpdateFlyingText(recent->second.text_id, entry.fa);
        console::AddLog(std::format("{} move_id:{} late stance fa:{}",
            is_p1 ? "p1:" : "p2:", entry.move_id, entry.fa));
        recent_entries.erase(recent);
    }

    void refresh_published_advantage(bool is_p1) {
        auto recent = recent_publications.find(is_p1);
        if (recent == recent_publications.end()) return;
        auto& displayed = recent->second.displayed;
        const auto& snapshot = displayed.snapshot;
        auto source = is_p1 ? snapshot->p1 : snapshot->p2;
        auto target = is_p1 ? snapshot->p2 : snapshot->p1;
        auto attacker = get(is_p1);
        auto receiver = get(!is_p1);
        auto previous = recent->second.previous;
        int age = state_log.back()->frame_count - snapshot->frame_count;
        bool timer_reset = attacker->raw_move_timer < previous->raw_move_timer;
        if (attacker->move_id != previous->move_id && previous->move_id == source->move_id &&
            source->startup > 0 && previous->raw_move_timer >= previous->recovery && attacker->startup == 0) {
            finished_attacks[is_p1] = { snapshot->frame_count,
                state_log[state_log.size() - 2]->frame_count + previous->frames_til_next_move };
        }
        auto finished = finished_attacks.find(is_p1);
        bool attack_finished = finished != finished_attacks.end() && finished->second.first == snapshot->frame_count;
        if (age < 0 || age > 180 || attacker->char_id != source->char_id ||
            receiver->char_id != target->char_id ||
            (receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE &&
             (source->startup == 0 || target->hit_outcome == MoveInfoEnums::HitOutcome::NONE ||
              receiver->complex_state != MoveInfoEnums::ComplexMoveStates::RECOVERING)) ||
            attacker->hit_outcome != MoveInfoEnums::HitOutcome::NONE ||
            (attack_finished && attacker->startup > 0) ||
            (!attack_finished && (
                attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::STANDING) ||
                attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::CROUCHING) ||
                (timer_reset && (attacker->startup > 0 || attacker->move_id == previous->move_id))))) {
            recent_publications.erase(recent);
            return;
        }
        recent->second.previous = attacker;
        int published = attacker->published_frame_advantage;
        int remaining_a = attacker->frames_til_next_move;
        if (attack_finished) remaining_a = finished->second.second - state_log.back()->frame_count;
        int remaining_d = receiver->frames_til_next_move;
        int advantage = remaining_d - remaining_a;
        auto previous_receiver = get(!is_p1, 1);
        bool shortened_recovery = receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE &&
            receiver->complex_state == MoveInfoEnums::ComplexMoveStates::RECOVERING &&
            previous_receiver && receiver->recovery < previous_receiver->recovery &&
            previous_receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE &&
            receiver->raw_move_timer > previous_receiver->raw_move_timer &&
            (attack_finished || attacker->move_id == source->move_id);
        bool paired_animation = attacker->move_id != source->move_id && receiver->move_id != target->move_id &&
            attacker->startup == 0 && receiver->startup == 0 &&
            attacker->raw_move_timer == 1 && receiver->raw_move_timer == 1 &&
            attacker->recovery > source->recovery && receiver->recovery > target->recovery &&
            receiver->simple_state == MoveInfoEnums::SimpleMoveStates::JUGGLED &&
            receiver->hit_outcome != MoveInfoEnums::HitOutcome::NONE;
        bool published_matches = published == advantage ||
            (remaining_a < 0 && receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE &&
             published == std::max(0, remaining_d) - std::max(0, remaining_a));
        // Follow recovery resets and timer-preserving multi-hit continuations.
        // Require a fresh signed pair corroborated by both recovery timers.
        if (!(paired_animation || shortened_recovery) && (published == source->published_frame_advantage ||
            static_cast<int64_t>(published) != -static_cast<int64_t>(receiver->published_frame_advantage) ||
            !published_matches)) return;
        std::string value = (advantage > 0 ? "+" : "") + std::to_string(advantage);
        auto projectile = pending_projectile.find(is_p1);
        if (projectile != pending_projectile.end() &&
            projectile->second.started->frame_count >= snapshot->frame_count &&
            projectile->second.source->move_id == attacker->move_id) {
            // This continuation is already represented by the parent label.
            pending_projectile.erase(projectile);
        }
        if (displayed.entry.fa != value) {
            overlay::UpdateFlyingText(displayed.text_id, value);
            console::AddLog(std::format("{} move_id:{} published fa:{}",
                is_p1 ? "p1:" : "p2:", displayed.entry.move_id, value));
            auto stance = recent_entries.find(is_p1);
            if (stance != recent_entries.end() && stance->second.text_id == displayed.text_id) {
                stance->second.entry.fa = value;
            }
        }
        recent_publications.erase(recent);
    }

    void read_player_state(bool is_p1) {
        auto attacker_now = get(is_p1);
        auto receiver_now = get(!is_p1);
        auto previous = get(is_p1, 1);
        auto target_before = get(!is_p1, 1);
        if (previous && target_before && previous->attack_type == MoveInfoEnums::AttackType::THROW &&
            (target_before->throw_tech == MoveInfoEnums::ThrowTechs::TE1 ||
             target_before->throw_tech == MoveInfoEnums::ThrowTechs::TE2 ||
             target_before->throw_tech == MoveInfoEnums::ThrowTechs::TE1_2) &&
            receiver_now->throw_tech == MoveInfoEnums::ThrowTechs::BROKEN_ThrowTechs &&
            attacker_now->move_id != previous->move_id && receiver_now->move_id != target_before->move_id &&
            attacker_now->startup == 0 && receiver_now->startup == 0) {
            auto break_entry = build(is_p1, previous);
            break_entry.move_id = "br: " + MoveInfoEnums::SafeEnum<MoveInfoEnums::ThrowTechs>::to_string(target_before->throw_tech);
            pending_break[is_p1] = { state_log.back(), break_entry };
        }
        refresh_published_advantage(is_p1);
        refresh_late_stance(is_p1);
        Entry entry;

        int pending_count = 0;
        auto pending_it = pending.find(is_p1);
        if (pending_it != pending.end()) {
            pending_count = pending_it->second;
        }

        auto heat = pending_heat.find(is_p1);
        auto projectile = pending_projectile.find(is_p1);
        auto broken = pending_break.find(is_p1);
        if (broken != pending_break.end()) {
            const auto& started = broken->second.started;
            auto source = is_p1 ? started->p1 : started->p2;
            auto target = is_p1 ? started->p2 : started->p1;
            int age = state_log.back()->frame_count - started->frame_count;
            if (age < 0 || age > 30 || attacker_now->char_id != source->char_id ||
                receiver_now->char_id != target->char_id || attacker_now->move_id != source->move_id ||
                receiver_now->move_id != target->move_id) {
                pending_break.erase(broken);
                return;
            }
            int advantage = receiver_now->frames_til_next_move - attacker_now->frames_til_next_move;
            if (attacker_now->raw_move_timer < 2 || receiver_now->raw_move_timer < 2 ||
                attacker_now->published_frame_advantage != advantage ||
                static_cast<int64_t>(receiver_now->published_frame_advantage) != -static_cast<int64_t>(advantage)) return;
            entry = broken->second.entry;
            pending_break.erase(broken);
            entry.fa = (advantage > 0 ? "+" : "") + std::to_string(advantage);
        }
        else if (projectile != pending_projectile.end()) {
            const auto& started = projectile->second.started;
            auto source = projectile->second.source;
            auto attacker = get(is_p1);
            auto receiver = get(!is_p1);
            auto target = is_p1 ? started->p2 : started->p1;
            int age = state_log.back()->frame_count - started->frame_count;
            if (age < 0 || age > 180 || attacker->char_id != source->char_id ||
                receiver->char_id != target->char_id ||
                attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::STANDING) ||
                attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::CROUCHING) ||
                (attacker->startup > 0 && attacker->move_id != source->move_id)) {
                pending_projectile.erase(projectile);
                return;
            }
            // Category 0x100003 has delayed contact and chained recoveries.
            // Corroborate the final publication with both remaining timers.
            int expected = receiver->frames_til_next_move - attacker->frames_til_next_move;
            bool contact = receiver->hit_outcome != MoveInfoEnums::HitOutcome::NONE;
            bool restarted_recovery = attacker->startup == 0 && attacker->move_id != source->move_id &&
                attacker->raw_move_timer >= 2 && attacker->raw_move_timer < source->startup;
            bool same_attack = attacker->move_id == source->move_id &&
                attacker->raw_move_timer > source->startup;
            bool published_pair = attacker->published_frame_advantage != source->published_frame_advantage &&
                static_cast<int64_t>(attacker->published_frame_advantage) ==
                -static_cast<int64_t>(receiver->published_frame_advantage);
            bool launch = same_attack && contact &&
                (receiver->simple_state == MoveInfoEnums::SimpleMoveStates::JUGGLED ||
                 receiver->simple_state == MoveInfoEnums::SimpleMoveStates::KNOCKDOWN);
            if (!(launch || (contact && attacker->published_frame_advantage == expected &&
                (restarted_recovery || (same_attack && published_pair))))) {
                return;
            }
            pending_projectile.erase(projectile);
            entry = build(is_p1, source);
            if (launch) {
                entry.fa = receiver->simple_state == MoveInfoEnums::SimpleMoveStates::JUGGLED ? "JGL" : "KND";
            }
            else {
                entry.fa = (expected > 0 ? "+" : "") + std::to_string(expected);
            }
        }
        else if (heat != pending_heat.end()) {
            const auto& started = heat->second.started;
            auto attacker = get(is_p1);
            auto receiver = get(!is_p1);
            auto source = is_p1 ? started->p1 : started->p2;
            auto target = is_p1 ? started->p2 : started->p1;
            int age = state_log.back()->frame_count - started->frame_count;
            if (age < 0 || age > 240 || attacker->char_id != source->char_id ||
                receiver->char_id != target->char_id ||
                (receiver->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_REACTION) &&
                 receiver->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RECOVERY) &&
                 receiver->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_REACTION_ALT) &&
                 receiver->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RECOVERY_ALT))) {
                pending_heat.erase(heat);
                return;
            }
            int advantage = attacker->published_frame_advantage;
            bool run_pair = (attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RUN) &&
                receiver->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RECOVERY)) ||
                (attacker->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RUN_ALT) &&
                 receiver->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_RECOVERY_ALT));
            if (!run_pair ||
                advantage == 0 || advantage == source->published_frame_advantage ||
                static_cast<int64_t>(advantage) != -static_cast<int64_t>(receiver->published_frame_advantage)) {
                return;
            }
            entry = heat->second.entry;
            entry.fa = (advantage > 0 ? "+" : "") + std::to_string(advantage);
            pending_heat.erase(heat);
        }
        else if (pending_count > 0) {
            auto receiver = get(!is_p1, 0);
            if (receiver != nullptr &&
                (receiver->complex_state == MoveInfoEnums::ComplexMoveStates::BLOCK || pending_count == 1)) {
                pending.erase(is_p1);
                entry = build(is_p1, pending_source[is_p1]);
                pending_source.erase(is_p1);
            }
            else {
                pending[is_p1] = pending_count - 1;
                return;
            }
        }
        else if (is_starting_attack(is_p1)) {
            auto source = get(is_p1, 1);
            if (source && source->attack_type == MoveInfoEnums::AttackType::SMID) {
                pending_projectile[is_p1] = { state_log.back(), source };
                return;
            }
            auto receiver = get(!is_p1, 0);
            if (is_block_transition(receiver)) {
                // the first blocked frame can still be a walk/guard state;
                // wait for the actual block reaction so fa is measured on it
                pending[is_p1] = 2;
                return;
            }
            if (receiver != nullptr &&
                (receiver->hit_outcome == MoveInfoEnums::HitOutcome::BLOCKED_STANDING ||
                    receiver->hit_outcome == MoveInfoEnums::HitOutcome::BLOCKED_CROUCHING)) {
                auto previous_attacker = get(is_p1, 1);
                auto current_attacker = get(is_p1, 0);
                if (previous_attacker != nullptr && current_attacker != nullptr &&
                    current_attacker->move_id == previous_attacker->move_id) {
                    // give the attacker one frame to enter its recovery
                    // animation (some moves switch to it a frame after the block)
                    pending[is_p1] = 1;
                    return;
                }
            }
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

        auto receiver = get(!is_p1);
        if (receiver->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_REACTION) ||
            receiver->move_id == static_cast<int>(MoveInfoEnums::UniversalMoves::HEAT_ENGAGER_REACTION_ALT)) {
            pending_heat[is_p1] = { state_log.back(), entry };
            return;
        }
        auto current = get(is_p1);
        if (get(is_p1, 1) && current->raw_move_timer == 1 &&
            !get_action_frame(current) && find_stance_attack(is_p1, current, 0)) {
            pending[is_p1] = 1;
            pending_source[is_p1] = get(is_p1, 1);
            return;
        }

        if (!entry.fa.empty()) {
            console::AddLog(std::format("{} char: {}({}) move_id:{} hit_type:{} complex_state:{} fa:{}",
                (entry.is_player ? "p1: " : "p2: "),
                entry.char_name,
                entry.char_id,
                entry.move_id,
                entry.hit_type,
                entry.complex_state,
                entry.fa
            ));
            const auto& camera = state_log.back()->camera;
            const auto anchor = entry.flying_origin && camera
                ? camera->project(*entry.flying_origin) : std::nullopt;
            auto text_id = overlay::AddFlyingText(entry.fa, anchor, entry.contact ? entry.hit_type : "",
                entry.flying_origin);
            recent_entries[is_p1] = { state_log.back(), entry, text_id };
            finished_attacks.erase(is_p1);
            recent_publications[is_p1] = { recent_entries[is_p1], get(is_p1) };
        }

        try {
            console::p1 = get(true, 0);
            console::p2 = get(false, 0);
        }
        catch (...) {
            console::AddLogError("exception");
        }
    }

    Entry build(bool is_p1, std::shared_ptr<PlayerSnapshot> source = nullptr) {
        Entry entry;
        auto previous = get(is_p1, 1);
        auto current = get(is_p1, 0);
        auto attacker = select_attacker(previous, current);
        auto receiver = get(!is_p1, 0);
        auto identity = source ? source : ((previous != nullptr) ? previous : current);

        entry.char_id = int(identity->char_id);
        entry.char_name = MoveInfoEnums::SafeEnum<MoveInfoEnums::CharacterCodes>::to_string(identity->char_id);
        entry.is_player = is_p1;
        entry.move_id = std::to_string(identity->move_id);
        entry.fa = get_fa(is_p1, attacker, receiver, attacker == current);
        entry.complex_state = MoveInfoEnums::SafeEnum<MoveInfoEnums::ComplexMoveStates>::to_string(receiver->complex_state);
        entry.hit_type = MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(identity->attack_type);
        entry.flying_origin = get_flying_origin(is_p1, source);
        entry.contact = receiver->hit_outcome != MoveInfoEnums::HitOutcome::NONE;

        return entry;
    }

    std::optional<WorldPoint> get_flying_origin(bool is_p1,
        const std::shared_ptr<PlayerSnapshot>& source = nullptr) const {
        if (state_log.empty()) return std::nullopt;
        const int now = state_log.back()->frame_count;
        int first_frame = now - 4;
        // Delayed projectile publications still belong to their original
        // attack. Heat entries already preserve the origin in their Entry.
        if (source) {
            for (auto it = state_log.rbegin(); it != state_log.rend(); ++it) {
                if (now - (*it)->frame_count > 180) break;
                if ((is_p1 ? (*it)->p1 : (*it)->p2) == source) {
                    first_frame = (*it)->frame_count;
                    break;
                }
            }
        }
        for (size_t i = state_log.size(); i-- > 1;) {
            if (state_log[i]->frame_count < first_frame) break;
            const auto& receiver = is_p1 ? state_log[i]->p2 : state_log[i]->p1;
            const auto& previous = is_p1 ? state_log[i - 1]->p2 : state_log[i - 1]->p1;
            if (!receiver->contact_position || receiver->char_id != previous->char_id ||
                receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE) continue;
            const bool fresh = previous->hit_outcome == MoveInfoEnums::HitOutcome::NONE ||
                receiver->contact_position != previous->contact_position ||
                receiver->damage_taken != previous->damage_taken ||
                receiver->raw_move_timer < previous->raw_move_timer;
            if (fresh) return receiver->contact_position;
        }
        // The first hitbox vector is updated into the active pose one frame
        // after the startup boundary. Look only at this attack's active window;
        // idle/recovery frames retain stale vectors from the preceding move.
        for (auto it = state_log.rbegin(); it != state_log.rend(); ++it) {
            if (now - (*it)->frame_count > 4) break;
            const auto& attacker = is_p1 ? (*it)->p1 : (*it)->p2;
            if (attacker->startup > 0 && attacker->raw_move_timer >= attacker->startup &&
                attacker->raw_move_timer <= attacker->startup + 2 && attacker->attack_hitbox_position) {
                return attacker->attack_hitbox_position;
            }
        }
        return std::nullopt;
    }

    static std::shared_ptr<PlayerSnapshot> select_attacker(
        const std::shared_ptr<PlayerSnapshot>& previous,
        const std::shared_ptr<PlayerSnapshot>& current) {
        // the entry is created a frame or two after the impact. by then the
        // attacker may have moved on to a separate recovery/recoil animation
        // (startup == 0), which is the state that actually determines how soon
        // they can act again. use it, but never use a state that belongs to a
        // newly started move.
        if (current == nullptr) {
            return previous;
        }
        if (previous == nullptr) {
            return current;
        }
        if (current->move_id == previous->move_id) {
            return current;
        }
        bool is_recovery =
            current->startup == 0 &&
            current->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::STANDING) &&
            current->move_id != static_cast<int>(MoveInfoEnums::UniversalMoves::CROUCHING) &&
            current->hit_outcome == MoveInfoEnums::HitOutcome::NONE &&
            current->stun_state == MoveInfoEnums::StunStates::NONE;
        return is_recovery ? current : previous;
    }

    static bool is_block_transition(const std::shared_ptr<PlayerSnapshot>& receiver) {
        if (receiver == nullptr) {
            return false;
        }
        return (receiver->hit_outcome == MoveInfoEnums::HitOutcome::BLOCKED_STANDING ||
                receiver->hit_outcome == MoveInfoEnums::HitOutcome::BLOCKED_CROUCHING) &&
            receiver->complex_state != MoveInfoEnums::ComplexMoveStates::BLOCK;
    }

    std::string get_fa(bool is_p1,
        std::shared_ptr<PlayerSnapshot> attacker,
        std::shared_ptr<PlayerSnapshot> receiver,
        bool use_raw_receiver = true) {
        if (attacker->attack_type == MoveInfoEnums::AttackType::THROW) {
            // Connected throw animations lock both sides even with outcome NONE.
            int advantage = receiver->frames_til_next_move - attacker->frames_til_next_move;
            return (advantage > 0 ? "+" : "") + std::to_string(advantage);
        }
        if (receiver->simple_state == MoveInfoEnums::SimpleMoveStates::KNOCKDOWN) {
            return "KND";
        }
        else if (receiver->hit_outcome == MoveInfoEnums::HitOutcome::JUGGLE) {
            return "JGL";
        }
        else {
            int time_till_recovery_p1 = get_frames_til_next_move(is_p1, attacker, use_raw_receiver ? 0 : 1);
            int time_till_recovery_p2;
            if (receiver->hit_outcome == MoveInfoEnums::HitOutcome::NONE) {
                time_till_recovery_p2 = 0;
            }
            else if (use_raw_receiver) {
                time_till_recovery_p2 = receiver->frames_til_next_move;
            }
            else {
                // the entry was created after the attacker had already moved
                // on; fall back to the old convention for the first reaction frame
                time_till_recovery_p2 = receiver->recovery - receiver->move_timer;
            }

            int raw_fa = time_till_recovery_p2 - time_till_recovery_p1;
            std::string raw_fa_str = std::to_string(raw_fa);

            if (raw_fa > 0) {
                raw_fa_str = "+" + raw_fa_str;
            }
            return raw_fa_str;
        }
    }
    

    void track_action_frame(bool is_p1, const std::shared_ptr<PlayerSnapshot>& player) {
        MoveKey key{ static_cast<int>(player->char_id), player->move_id };
        auto previous = last_action_field.find(is_p1);
        bool published = previous != last_action_field.end() &&
            previous->second.first.first == key.first &&
            previous->second.second != player->actionable_frame;
        last_action_field[is_p1] = { key, player->actionable_frame };
        // A change on the very first stance frame is a publication too (Lars).
        if (published && player->actionable_frame > 0 && player->actionable_frame < player->recovery) {
            auto existing = action_frames.find(key);
            if (existing != action_frames.end() && existing->second == player->actionable_frame) return;
            action_frames[key] = player->actionable_frame;
            if (!action_frame_file.empty() && !ActionFrameStore::save(action_frame_file, action_frames)) {
                console::AddLogError("Could not save actionable frames: " + action_frame_file);
            }
        }
    }

    std::optional<int> get_action_frame(const std::shared_ptr<PlayerSnapshot>& player) const {
        auto found = action_frames.find({ static_cast<int>(player->char_id), player->move_id });
        if (found != action_frames.end() && found->second > 0 && found->second < player->recovery) {
            return found->second;
        }
        return std::nullopt;
    }

    std::optional<std::pair<std::shared_ptr<PlayerSnapshot>, int>> find_stance_attack(
        bool is_p1, const std::shared_ptr<PlayerSnapshot>& attacker, int frames_ago) {
        if (attacker->startup != 0 || attacker->hit_outcome != MoveInfoEnums::HitOutcome::NONE ||
            attacker->stun_state != MoveInfoEnums::StunStates::NONE) {
            return std::nullopt;
        }
        for (int extra = 1; extra <= 60; ++extra) {
            int elapsed = frames_ago + extra;
            auto attack = get(is_p1, elapsed);
            if (!attack) return std::nullopt;
            if (attack->move_id == attacker->move_id || attack->startup == 0) continue;
            if (attacker->recovery <= attack->recovery || attacker->raw_move_timer > elapsed) {
                return std::nullopt;
            }
            return std::make_pair(attack, elapsed);
        }
        return std::nullopt;
    }

    int get_frames_til_next_move(bool is_p1, const std::shared_ptr<PlayerSnapshot>& attacker, int frames_ago) {
        if (auto action = get_action_frame(attacker)) {
            return *action - attacker->raw_move_timer;
        }
        if (auto stance = find_stance_attack(is_p1, attacker, frames_ago)) {
            return stance->first->frames_til_next_move - stance->second -
                (attacker->recovery - stance->first->recovery);
        }
        return attacker->frames_til_next_move;
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
