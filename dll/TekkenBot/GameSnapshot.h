#pragma once
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <Windows.h>
#include <iostream>
#include "MoveInfoEnums.h"
#include "ConfigParser.h"

#undef max
#undef min

class PlayerSnapshot {
public:
    int move_id;
    MoveInfoEnums::SimpleMoveStates simple_state;
    MoveInfoEnums::AttackType attack_type;
    int startup;
    MoveInfoEnums::ComplexMoveStates complex_state;
    int damage_taken;
    int move_timer;
    int recovery;
    int frames_til_next_move;
    MoveInfoEnums::CharacterCodes char_id;
    MoveInfoEnums::ThrowTechs throw_tech;
    MoveInfoEnums::InputDirectionCodes input_direction;
    MoveInfoEnums::InputAttackCodes input_button;
    MoveInfoEnums::StunStates stun_state;
    MoveInfoEnums::HitOutcome hit_outcome;

    PlayerSnapshot(const std::map<std::string, int>& player_data_dict) {
        move_id = player_data_dict.at("move_id");
        simple_state = static_cast<MoveInfoEnums::SimpleMoveStates>(player_data_dict.at("simple_move_state"));
        attack_type = static_cast<MoveInfoEnums::AttackType>(player_data_dict.at("attack_type"));
        startup = player_data_dict.at("attack_startup");
        complex_state = static_cast<MoveInfoEnums::ComplexMoveStates>(player_data_dict.at("complex_move_state"));
        damage_taken = player_data_dict.at("damage_taken");
        move_timer = (startup == 0) ? 0 : player_data_dict.at("move_timer");
        recovery = player_data_dict.at("recovery");
        frames_til_next_move = recovery - move_timer;
        char_id = static_cast<MoveInfoEnums::CharacterCodes>(player_data_dict.at("char_id"));
        throw_tech = static_cast<MoveInfoEnums::ThrowTechs>(player_data_dict.at("throw_tech"));
        input_direction = static_cast<MoveInfoEnums::InputDirectionCodes>(player_data_dict.at("input_direction"));
        input_button = static_cast<MoveInfoEnums::InputAttackCodes>(
            player_data_dict.at("input_attack") % static_cast<int>(MoveInfoEnums::InputAttackCodes::xRAGE));
        stun_state = static_cast<MoveInfoEnums::StunStates>(player_data_dict.at("stun_type"));
        hit_outcome = static_cast<MoveInfoEnums::HitOutcome>(player_data_dict.at("hit_outcome"));
    }
};

class GameSnapshot {
public:
    std::shared_ptr<PlayerSnapshot> p1;
    std::shared_ptr<PlayerSnapshot> p2;
    int frame_count;
    bool facing_bool;

    GameSnapshot(std::shared_ptr<PlayerSnapshot> p1,
        std::shared_ptr<PlayerSnapshot> p2,
        int frame_count,
        bool facing_bool)
        : p1(p1), p2(p2), frame_count(frame_count), facing_bool(facing_bool) {
    }
};
