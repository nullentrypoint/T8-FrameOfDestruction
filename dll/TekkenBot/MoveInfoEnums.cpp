#include "MoveInfoEnums.h"

namespace MoveInfoEnums {
    // ������� ���������� ������� SafeEnum
    template<typename T>
    std::unordered_map<long long, T>& SafeEnum<T>::value_map() {
        static std::unordered_map<long long, T> map;
        return map;
    }

    template<typename T>
    std::unordered_map<T, std::string>& SafeEnum<T>::string_map() {
        static std::unordered_map<T, std::string> map;
        return map;
    }

    template<typename T>
    T SafeEnum<T>::from_int(long long value) {
        auto it = value_map().find(value);
        return (it != value_map().end()) ? it->second : static_cast<T>(0);
    }

    template<typename T>
    std::string SafeEnum<T>::to_string(T value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<ComplexMoveStates>::to_string(ComplexMoveStates value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<CharacterCodes>::to_string(CharacterCodes value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<ThrowTechs>::to_string(ThrowTechs value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<InputAttackCodes>::to_string(InputAttackCodes value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<AttackType>::to_string(AttackType value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<StunStates>::to_string(StunStates value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<HitOutcome>::to_string(HitOutcome value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<InputDirectionCodes>::to_string(InputDirectionCodes value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    std::string SafeEnum<SimpleMoveStates>::to_string(SimpleMoveStates value) {
        auto it = string_map().find(value);
        if (it != string_map().end()) {
            return it->second;
        }
        return "UNKNOWN";
    }

    template<typename T>
    T SafeEnum<T>::from_string(const std::string& name) {
        for (const auto& pair : string_map()) {
            if (pair.second == name) {
                return pair.first;
            }
        }
        return static_cast<T>(0);
    }

    InputAttackCodes SafeEnum<InputAttackCodes>::from_string(const std::string& name) {
        for (const auto& pair : string_map()) {
            if (pair.second == name) {
                return pair.first;
            }
        }
        return static_cast<InputAttackCodes>(0);
    }

    template<>
    std::unordered_map<long long, UniversalMoves>& SafeEnum<UniversalMoves>::value_map() {
        static std::unordered_map<long long, UniversalMoves> map = {
            {403, UniversalMoves::HEAT_ENGAGER_RUN},
            {365, UniversalMoves::HEAT_ENGAGER_RUN_ALT},
            {377, UniversalMoves::HEAT_ENGAGER_REACTION_ALT},
            {378, UniversalMoves::HEAT_ENGAGER_RECOVERY_ALT},
            {417, UniversalMoves::HEAT_ENGAGER_REACTION},
            {418, UniversalMoves::HEAT_ENGAGER_RECOVERY},
            {32769, UniversalMoves::STANDING},
            {32770, UniversalMoves::CROUCHING}
        };
        return map;
    }

    template<>
    std::unordered_map<UniversalMoves, std::string>& SafeEnum<UniversalMoves>::string_map() {
        static std::unordered_map<UniversalMoves, std::string> map = {
            {UniversalMoves::HEAT_ENGAGER_RUN, "HEAT_ENGAGER_RUN"},
            {UniversalMoves::HEAT_ENGAGER_RUN_ALT, "HEAT_ENGAGER_RUN_ALT"},
            {UniversalMoves::HEAT_ENGAGER_REACTION_ALT, "HEAT_ENGAGER_REACTION_ALT"},
            {UniversalMoves::HEAT_ENGAGER_RECOVERY_ALT, "HEAT_ENGAGER_RECOVERY_ALT"},
            {UniversalMoves::HEAT_ENGAGER_REACTION, "HEAT_ENGAGER_REACTION"},
            {UniversalMoves::HEAT_ENGAGER_RECOVERY, "HEAT_ENGAGER_RECOVERY"},
            {UniversalMoves::STANDING, "STANDING"},
            {UniversalMoves::CROUCHING, "CROUCHING"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, AttackType>& SafeEnum<AttackType>::value_map() {
        static std::unordered_map<long long, AttackType> map = {
            {0, AttackType::NA},
            {6291466, AttackType::THROW},
            {12582919, AttackType::MID_UNBLOCKABLE},
            {10485765, AttackType::HIGH},
            {6291460, AttackType::SLOW},
            {8388610, AttackType::MID},
            {2097153, AttackType::LOW},
            {12582912, AttackType::RECOVERING},
            {1048579, AttackType::SMID}
        };
        return map;
    }

    template<>
    std::unordered_map<AttackType, std::string>& SafeEnum<AttackType>::string_map() {
        static std::unordered_map<AttackType, std::string> map = {
            {AttackType::NA, "NA"},
            {AttackType::THROW, "THROW"},
            {AttackType::MID_UNBLOCKABLE, "MID_UNBLOCKABLE"},
            {AttackType::HIGH, "HIGH"},
            {AttackType::SLOW, "S.LOW"},
            {AttackType::MID, "MID"},
            {AttackType::LOW, "LOW"},
            {AttackType::RECOVERING, "RECOVERING"},
            {AttackType::SMID, "S.MID"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, SimpleMoveStates>& SafeEnum<SimpleMoveStates>::value_map() {
        static std::unordered_map<long long, SimpleMoveStates> map = {
            {0, SimpleMoveStates::UNINITIALIZED},
            {1, SimpleMoveStates::STANDING_FORWARD},
            {2, SimpleMoveStates::STANDING_BACK},
            {3, SimpleMoveStates::STANDING},
            {4, SimpleMoveStates::STEVE},
            {5, SimpleMoveStates::CROUCH_FORWARD},
            {6, SimpleMoveStates::CROUCH_BACK},
            {7, SimpleMoveStates::CROUCH},
            {8, SimpleMoveStates::OFF_AXIS_GETUP},
            {9, SimpleMoveStates::UNKNOWN_TYPE_9},
            {10, SimpleMoveStates::UNKNOWN_10},
            {11, SimpleMoveStates::UNKNOWN_GETUP_11},
            {12, SimpleMoveStates::GROUND_FACEUP},
            {13, SimpleMoveStates::GROUND_FACEDOWN},
            {14, SimpleMoveStates::JUGGLED},
            {15, SimpleMoveStates::KNOCKDOWN},
            {18, SimpleMoveStates::WALL_SPLAT_18},
            {19, SimpleMoveStates::WALL_SPLAT_19},
            {20, SimpleMoveStates::TECH_ROLL_OR_FLOOR_BREAK},
            {23, SimpleMoveStates::UNKNOWN_23},
            {24, SimpleMoveStates::AIRBORNE_24},
            {25, SimpleMoveStates::AIRBORNE},
            {26, SimpleMoveStates::AIRBORNE_26},
            {27, SimpleMoveStates::FLY}
        };
        return map;
    }

    template<>
    std::unordered_map<SimpleMoveStates, std::string>& SafeEnum<SimpleMoveStates>::string_map() {
        static std::unordered_map<SimpleMoveStates, std::string> map = {
            {SimpleMoveStates::UNINITIALIZED, "UNINITIALIZED"},
            {SimpleMoveStates::STANDING_FORWARD, "STANDING_FORWARD"},
            {SimpleMoveStates::STANDING_BACK, "STANDING_BACK"},
            {SimpleMoveStates::STANDING, "STANDING"},
            {SimpleMoveStates::STEVE, "STEVE"},
            {SimpleMoveStates::CROUCH_FORWARD, "CROUCH_FORWARD"},
            {SimpleMoveStates::CROUCH_BACK, "CROUCH_BACK"},
            {SimpleMoveStates::CROUCH, "CROUCH"},
            {SimpleMoveStates::OFF_AXIS_GETUP, "OFF_AXIS_GETUP"},
            {SimpleMoveStates::UNKNOWN_TYPE_9, "UNKNOWN_TYPE_9"},
            {SimpleMoveStates::UNKNOWN_10, "UNKNOWN_10"},
            {SimpleMoveStates::UNKNOWN_GETUP_11, "UNKNOWN_GETUP_11"},
            {SimpleMoveStates::GROUND_FACEUP, "GROUND_FACEUP"},
            {SimpleMoveStates::GROUND_FACEDOWN, "GROUND_FACEDOWN"},
            {SimpleMoveStates::JUGGLED, "JUGGLED"},
            {SimpleMoveStates::KNOCKDOWN, "KNOCKDOWN"},
            {SimpleMoveStates::WALL_SPLAT_18, "WALL_SPLAT_18"},
            {SimpleMoveStates::WALL_SPLAT_19, "WALL_SPLAT_19"},
            {SimpleMoveStates::TECH_ROLL_OR_FLOOR_BREAK, "TECH_ROLL_OR_FLOOR_BREAK"},
            {SimpleMoveStates::UNKNOWN_23, "UNKNOWN_23"},
            {SimpleMoveStates::AIRBORNE_24, "AIRBORNE_24"},
            {SimpleMoveStates::AIRBORNE, "AIRBORNE"},
            {SimpleMoveStates::AIRBORNE_26, "AIRBORNE_26"},
            {SimpleMoveStates::FLY, "FLY"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, ComplexMoveStates>& SafeEnum<ComplexMoveStates>::value_map() {
        static std::unordered_map<long long, ComplexMoveStates> map = {
            {0, ComplexMoveStates::F_MINUS},
            {1, ComplexMoveStates::S_PLUS},
            {2, ComplexMoveStates::S},
            {3, ComplexMoveStates::A},
            {4, ComplexMoveStates::UN04},
            {5, ComplexMoveStates::C_MINUS},
            {6, ComplexMoveStates::A_PLUS},
            {7, ComplexMoveStates::C},
            {10, ComplexMoveStates::END1},
            {11, ComplexMoveStates::BLOCK},
            {12, ComplexMoveStates::WALK},
            {13, ComplexMoveStates::SIDEROLL_GETUP},
            {14, ComplexMoveStates::SIDEROLL_STAYDOWN},
            {15, ComplexMoveStates::SS},
            {16, ComplexMoveStates::RECOVERING},
            {17, ComplexMoveStates::UN17},
            {18, ComplexMoveStates::UN18},
            {20, ComplexMoveStates::UN20},
            {22, ComplexMoveStates::UN22},
            {23, ComplexMoveStates::UN23},
            {28, ComplexMoveStates::SW},
            {54, ComplexMoveStates::BROKEN_54},
            {999999, ComplexMoveStates::UNKN}
        };
        return map;
    }

    template<>
    std::unordered_map<ComplexMoveStates, std::string>& SafeEnum<ComplexMoveStates>::string_map() {
        static std::unordered_map<ComplexMoveStates, std::string> map = {
            {ComplexMoveStates::F_MINUS, "F_MINUS"},
            {ComplexMoveStates::S_PLUS, "S_PLUS"},
            {ComplexMoveStates::S, "S"},
            {ComplexMoveStates::A, "A"},
            {ComplexMoveStates::UN04, "UN04"},
            {ComplexMoveStates::C_MINUS, "C_MINUS"},
            {ComplexMoveStates::A_PLUS, "A_PLUS"},
            {ComplexMoveStates::C, "C"},
            {ComplexMoveStates::END1, "END1"},
            {ComplexMoveStates::BLOCK, "BLOCK"},
            {ComplexMoveStates::WALK, "WALK"},
            {ComplexMoveStates::SIDEROLL_GETUP, "SIDEROLL_GETUP"},
            {ComplexMoveStates::SIDEROLL_STAYDOWN, "SIDEROLL_STAYDOWN"},
            {ComplexMoveStates::SS, "SS"},
            {ComplexMoveStates::RECOVERING, "RECOVERING"},
            {ComplexMoveStates::UN17, "UN17"},
            {ComplexMoveStates::UN18, "UN18"},
            {ComplexMoveStates::UN20, "UN20"},
            {ComplexMoveStates::UN22, "UN22"},
            {ComplexMoveStates::UN23, "UN23"},
            {ComplexMoveStates::SW, "SW"},
            {ComplexMoveStates::BROKEN_54, "BROKEN_54"},
            {ComplexMoveStates::UNKN, "UNKN"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, ThrowTechs>& SafeEnum<ThrowTechs>::value_map() {
        static std::unordered_map<long long, ThrowTechs> map = {
            {0, ThrowTechs::NONE},
            {3489660956, ThrowTechs::TE1},
            {3758096415, ThrowTechs::TE2},
            {4026531870, ThrowTechs::TE1_2},
            {3221225501, ThrowTechs::BROKEN_ThrowTechs}
        };
        return map;
    }

    template<>
    std::unordered_map<ThrowTechs, std::string>& SafeEnum<ThrowTechs>::string_map() {
        static std::unordered_map<ThrowTechs, std::string> map = {
            {ThrowTechs::NONE, "NONE"},
            {ThrowTechs::TE1, "TE1"},
            {ThrowTechs::TE2, "TE2"},
            {ThrowTechs::TE1_2, "TE1_2"},
            {ThrowTechs::BROKEN_ThrowTechs, "BROKEN_ThrowTechs"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, StunStates>& SafeEnum<StunStates>::value_map() {
        static std::unordered_map<long long, StunStates> map = {
            {0, StunStates::NONE},
            {2, StunStates::UNKNOWN_2},
            {0x01000100, StunStates::BLOCK},
            {0x100, StunStates::GETTING_HIT},
            {0x10000, StunStates::DOING_THE_HITTING},
            {0x10100, StunStates::BEING_PUNISHED},
            {0x1000000, StunStates::BLOCK_NO_HIT}
        };
        return map;
    }

    template<>
    std::unordered_map<StunStates, std::string>& SafeEnum<StunStates>::string_map() {
        static std::unordered_map<StunStates, std::string> map = {
            {StunStates::NONE, "NONE"},
            {StunStates::UNKNOWN_2, "UNKNOWN_2"},
            {StunStates::BLOCK, "BLOCK"},
            {StunStates::GETTING_HIT, "GETTING_HIT"},
            {StunStates::DOING_THE_HITTING, "DOING_THE_HITTING"},
            {StunStates::BEING_PUNISHED, "BEING_PUNISHED"},
            {StunStates::BLOCK_NO_HIT, "BLOCK_NO_HIT"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, HitOutcome>& SafeEnum<HitOutcome>::value_map() {
        static std::unordered_map<long long, HitOutcome> map = {
            {0, HitOutcome::NONE},
            {1, HitOutcome::BLOCKED_STANDING},
            {2, HitOutcome::BLOCKED_CROUCHING},
            {3, HitOutcome::JUGGLE},
            {4, HitOutcome::SCREW},
            {5, HitOutcome::UNKNOWN_SCREW_5},
            {6, HitOutcome::UNKNOWN_6},
            {7, HitOutcome::UNKNOWN_SCREW_7},
            {8, HitOutcome::GROUNDED_FACE_DOWN},
            {9, HitOutcome::GROUNDED_FACE_UP},
            {10, HitOutcome::COUNTER_HIT_STANDING},
            {11, HitOutcome::COUNTER_HIT_CROUCHING},
            {12, HitOutcome::NORMAL_HIT_STANDING},
            {13, HitOutcome::NORMAL_HIT_CROUCHING},
            {14, HitOutcome::NORMAL_HIT_STANDING_LEFT},
            {15, HitOutcome::NORMAL_HIT_CROUCHING_LEFT},
            {16, HitOutcome::NORMAL_HIT_STANDING_BACK},
            {17, HitOutcome::NORMAL_HIT_CROUCHING_BACK},
            {18, HitOutcome::NORMAL_HIT_STANDING_RIGHT},
            {19, HitOutcome::NORMAL_HIT_CROUCHING_RIGHT}
        };
        return map;
    }

    template<>
    std::unordered_map<HitOutcome, std::string>& SafeEnum<HitOutcome>::string_map() {
        static std::unordered_map<HitOutcome, std::string> map = {
            {HitOutcome::NONE, "NONE"},
            {HitOutcome::BLOCKED_STANDING, "BLOCKED_STANDING"},
            {HitOutcome::BLOCKED_CROUCHING, "BLOCKED_CROUCHING"},
            {HitOutcome::JUGGLE, "JUGGLE"},
            {HitOutcome::SCREW, "SCREW"},
            {HitOutcome::UNKNOWN_SCREW_5, "UNKNOWN_SCREW_5"},
            {HitOutcome::UNKNOWN_6, "UNKNOWN_6"},
            {HitOutcome::UNKNOWN_SCREW_7, "UNKNOWN_SCREW_7"},
            {HitOutcome::GROUNDED_FACE_DOWN, "GROUNDED_FACE_DOWN"},
            {HitOutcome::GROUNDED_FACE_UP, "GROUNDED_FACE_UP"},
            {HitOutcome::COUNTER_HIT_STANDING, "COUNTER_HIT_STANDING"},
            {HitOutcome::COUNTER_HIT_CROUCHING, "COUNTER_HIT_CROUCHING"},
            {HitOutcome::NORMAL_HIT_STANDING, "NORMAL_HIT_STANDING"},
            {HitOutcome::NORMAL_HIT_CROUCHING, "NORMAL_HIT_CROUCHING"},
            {HitOutcome::NORMAL_HIT_STANDING_LEFT, "NORMAL_HIT_STANDING_LEFT"},
            {HitOutcome::NORMAL_HIT_CROUCHING_LEFT, "NORMAL_HIT_CROUCHING_LEFT"},
            {HitOutcome::NORMAL_HIT_STANDING_BACK, "NORMAL_HIT_STANDING_BACK"},
            {HitOutcome::NORMAL_HIT_CROUCHING_BACK, "NORMAL_HIT_CROUCHING_BACK"},
            {HitOutcome::NORMAL_HIT_STANDING_RIGHT, "NORMAL_HIT_STANDING_RIGHT"},
            {HitOutcome::NORMAL_HIT_CROUCHING_RIGHT, "NORMAL_HIT_CROUCHING_RIGHT"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, InputDirectionCodes>& SafeEnum<InputDirectionCodes>::value_map() {
        static std::unordered_map<long long, InputDirectionCodes> map = {
            {0, InputDirectionCodes::_NULL},
            {0x20, InputDirectionCodes::N},
            {0x100, InputDirectionCodes::u},
            {0x80, InputDirectionCodes::ub},
            {0x200, InputDirectionCodes::uf},
            {0x40, InputDirectionCodes::f},
            {0x10, InputDirectionCodes::b},
            {4, InputDirectionCodes::d},
            {8, InputDirectionCodes::df},
            {2, InputDirectionCodes::db}
        };
        return map;
    }

    template<>
    std::unordered_map<InputDirectionCodes, std::string>& SafeEnum<InputDirectionCodes>::string_map() {
        static std::unordered_map<InputDirectionCodes, std::string> map = {
            {InputDirectionCodes::_NULL, "NULL"},
            {InputDirectionCodes::N, "N"},
            {InputDirectionCodes::u, "u"},
            {InputDirectionCodes::ub, "ub"},
            {InputDirectionCodes::uf, "uf"},
            {InputDirectionCodes::f, "f"},
            {InputDirectionCodes::b, "b"},
            {InputDirectionCodes::d, "d"},
            {InputDirectionCodes::df, "df"},
            {InputDirectionCodes::db, "db"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, InputAttackCodes>& SafeEnum<InputAttackCodes>::value_map() {
        static std::unordered_map<long long, InputAttackCodes> map = {
            {0, InputAttackCodes::N},
            {512, InputAttackCodes::x1},
            {1024, InputAttackCodes::x2},
            {2048, InputAttackCodes::x3},
            {4096, InputAttackCodes::x4},
            {1536, InputAttackCodes::x1x2},
            {2560, InputAttackCodes::x1x3},
            {4608, InputAttackCodes::x1x4},
            {3072, InputAttackCodes::x2x3},
            {5120, InputAttackCodes::x2x4},
            {6144, InputAttackCodes::x3x4},
            {3584, InputAttackCodes::x1x2x3},
            {5632, InputAttackCodes::x1x2x4},
            {6656, InputAttackCodes::x1x3x4},
            {7168, InputAttackCodes::x2x3x4},
            {7680, InputAttackCodes::x1x2x3x4},
            {8192, InputAttackCodes::xRAGE}
        };
        return map;
    }

    template<>
    std::unordered_map<InputAttackCodes, std::string>& SafeEnum<InputAttackCodes>::string_map() {
        static std::unordered_map<InputAttackCodes, std::string> map = {
            {InputAttackCodes::N, "N"},
            {InputAttackCodes::x1, "x1"},
            {InputAttackCodes::x2, "x2"},
            {InputAttackCodes::x3, "x3"},
            {InputAttackCodes::x4, "x4"},
            {InputAttackCodes::x1x2, "x1x2"},
            {InputAttackCodes::x1x3, "x1x3"},
            {InputAttackCodes::x1x4, "x1x4"},
            {InputAttackCodes::x2x3, "x2x3"},
            {InputAttackCodes::x2x4, "x2x4"},
            {InputAttackCodes::x3x4, "x3x4"},
            {InputAttackCodes::x1x2x3, "x1x2x3"},
            {InputAttackCodes::x1x2x4, "x1x2x4"},
            {InputAttackCodes::x1x3x4, "x1x3x4"},
            {InputAttackCodes::x2x3x4, "x2x3x4"},
            {InputAttackCodes::x1x2x3x4, "x1x2x3x4"},
            {InputAttackCodes::xRAGE, "xRAGE"}
        };
        return map;
    }

    template<>
    std::unordered_map<long long, CharacterCodes>& SafeEnum<CharacterCodes>::value_map() {
        static std::unordered_map<long long, CharacterCodes> map = {
            {0, CharacterCodes::PAUL},
            {1, CharacterCodes::LAW},
            {2, CharacterCodes::KING},
            {3, CharacterCodes::YOSHIMITSU},
            {4, CharacterCodes::HWOARANG},
            {5, CharacterCodes::XIAOYU},
            {6, CharacterCodes::JIN},
            {7, CharacterCodes::BRYAN},
            {8, CharacterCodes::KAZUYA},
            {9, CharacterCodes::STEVE},
            {10, CharacterCodes::JACK},
            {11, CharacterCodes::ASUKA},
            {12, CharacterCodes::DEVIL_JIN},
            {13, CharacterCodes::FENG},
            {14, CharacterCodes::LILI},
            {15, CharacterCodes::DRAGUNOV},
            {16, CharacterCodes::LEO},
            {17, CharacterCodes::LARS},
            {18, CharacterCodes::ALISA},
            {19, CharacterCodes::CLAUDIO},
            {20, CharacterCodes::SHAHEEN},
            {21, CharacterCodes::NINA},
            {22, CharacterCodes::LEE},
            {23, CharacterCodes::KUMA},
            {24, CharacterCodes::PANDA},
            {25, CharacterCodes::ZAFINA},
            {26, CharacterCodes::LEROY},
            {27, CharacterCodes::JUN},
            {28, CharacterCodes::REINA},
            {29, CharacterCodes::AZUCENA},
            {30, CharacterCodes::VICTOR},
            {31, CharacterCodes::RAVEN},
            {33, CharacterCodes::EDDY},
            {34, CharacterCodes::LIDIA},
            {35, CharacterCodes::HEIHACHI},
            {36, CharacterCodes::CLIVE},
            {37, CharacterCodes::ANNA},
            {38, CharacterCodes::FAHKUMRAM},
            {39, CharacterCodes::ARMOR_KING},
            {40, CharacterCodes::MIARY_ZO},
            {41, CharacterCodes::KUNIMITSU},
            {116, CharacterCodes::_DUMMY}
        };
        return map;
    }

    template<>
    std::unordered_map<CharacterCodes, std::string>& SafeEnum<CharacterCodes>::string_map() {
        static std::unordered_map<CharacterCodes, std::string> map = {
            {CharacterCodes::PAUL, "PAUL"},
            {CharacterCodes::LAW, "LAW"},
            {CharacterCodes::KING, "KING"},
            {CharacterCodes::YOSHIMITSU, "YOSHIMITSU"},
            {CharacterCodes::HWOARANG, "HWOARANG"},
            {CharacterCodes::XIAOYU, "XIAOYU"},
            {CharacterCodes::JIN, "JIN"},
            {CharacterCodes::BRYAN, "BRYAN"},
            {CharacterCodes::KAZUYA, "KAZUYA"},
            {CharacterCodes::STEVE, "STEVE"},
            {CharacterCodes::JACK, "JACK"},
            {CharacterCodes::ASUKA, "ASUKA"},
            {CharacterCodes::DEVIL_JIN, "DEVIL_JIN"},
            {CharacterCodes::FENG, "FENG"},
            {CharacterCodes::LILI, "LILI"},
            {CharacterCodes::DRAGUNOV, "DRAGUNOV"},
            {CharacterCodes::LEO, "LEO"},
            {CharacterCodes::LARS, "LARS"},
            {CharacterCodes::ALISA, "ALISA"},
            {CharacterCodes::CLAUDIO, "CLAUDIO"},
            {CharacterCodes::SHAHEEN, "SHAHEEN"},
            {CharacterCodes::NINA, "NINA"},
            {CharacterCodes::LEE, "LEE"},
            {CharacterCodes::KUMA, "KUMA"},
            {CharacterCodes::PANDA, "PANDA"},
            {CharacterCodes::ZAFINA, "ZAFINA"},
            {CharacterCodes::LEROY, "LEROY"},
            {CharacterCodes::JUN, "JUN"},
            {CharacterCodes::REINA, "REINA"},
            {CharacterCodes::AZUCENA, "AZUCENA"},
            {CharacterCodes::VICTOR, "VICTOR"},
            {CharacterCodes::RAVEN, "RAVEN"},
            {CharacterCodes::EDDY, "EDDY"},
            {CharacterCodes::LIDIA, "LIDIA"},
            {CharacterCodes::HEIHACHI, "HEIHACHI"},
            {CharacterCodes::CLIVE, "CLIVE"},
            {CharacterCodes::ANNA, "ANNA"},
            {CharacterCodes::FAHKUMRAM, "FAHKUMRAM"},
            {CharacterCodes::ARMOR_KING, "ARMOR_KING"},
            {CharacterCodes::MIARY_ZO, "MIARY_ZO"},
            {CharacterCodes::KUNIMITSU, "KUNIMITSU"},
            {CharacterCodes::_DUMMY, "_DUMMY"}
        };
        return map;
    }
}
