#pragma once
#include <string>
#include <stdexcept>
#include <unordered_map>

namespace MoveInfoEnums {

    // Базовый шаблонный класс для безопасных перечислений
    template<typename T>
    class SafeEnum {
    protected:
        static std::unordered_map<long long, T>& value_map();

        static std::unordered_map<T, std::string>& string_map();

    public:
        static T from_int(long long value);

        static std::string to_string(T value);

        static T from_string(const std::string& name);
    };

    // UniversalMoves
    enum class UniversalMoves: long long {
        STANDING = 32769,
        CROUCHING = 32770
    };

    // AttackType
    enum class AttackType: long long {
        NA = 0,
        THROW = 6291466,
        MID_UNBLOCKABLE = 12582919,
        HIGH = 10485765,
        SMID = 6291460,
        MID = 8388610,
        LOW = 2097153,
        RECOVERING = 12582912,
        UNKNOWN = 1048579
    };

    // SimpleMoveStates
    enum class SimpleMoveStates: long long {
        UNINITIALIZED = 0,
        STANDING_FORWARD = 1,
        STANDING_BACK = 2,
        STANDING = 3,
        STEVE = 4,
        CROUCH_FORWARD = 5,
        CROUCH_BACK = 6,
        CROUCH = 7,
        OFF_AXIS_GETUP = 8,
        UNKNOWN_TYPE_9 = 9,
        GROUND_FACEUP = 12,
        GROUND_FACEDOWN = 13,
        JUGGLED = 14,
        KNOCKDOWN = 15,
        UNKNOWN_10 = 10,
        UNKNOWN_GETUP_11 = 11,
        WALL_SPLAT_18 = 18,
        WALL_SPLAT_19 = 19,
        TECH_ROLL_OR_FLOOR_BREAK = 20,
        UNKNOWN_23 = 23,
        AIRBORNE_24 = 24,
        AIRBORNE = 25,
        AIRBORNE_26 = 26,
        FLY = 27
    };

    // ComplexMoveStates
    enum class ComplexMoveStates: long long {
        F_MINUS = 0,
        S_PLUS = 1,
        S = 2,
        A = 3,
        UN04 = 4,
        C_MINUS = 5,
        A_PLUS = 6,
        C = 7,
        END1 = 10,
        BLOCK = 11,
        WALK = 12,
        SIDEROLL_GETUP = 13,
        SIDEROLL_STAYDOWN = 14,
        SS = 15,
        RECOVERING = 16,
        UN17 = 17,
        UN18 = 18,
        UN20 = 20,
        UN22 = 22,
        UN23 = 23,
        SW = 28,
        BROKEN_54 = 54,
        UNKN = 999999
    };

    // ThrowTechs
    enum class ThrowTechs: long long {
        NONE = 0,
        TE1 = 3489660956,
        TE2 = 3758096415,
        TE1_2 = 4026531870,
        BROKEN_ThrowTechs = 3221225501
    };

    // StunStates
    enum class StunStates: long long {
        NONE = 0,
        UNKNOWN_2 = 2,
        BLOCK = 0x01000100,
        GETTING_HIT = 0x100,
        DOING_THE_HITTING = 0x10000,
        BEING_PUNISHED = 0x10100,
        BLOCK_NO_HIT = 0x1000000
    };

    // HitOutcome
    enum class HitOutcome: long long {
        NONE = 0,
        BLOCKED_STANDING = 1,
        BLOCKED_CROUCHING = 2,
        JUGGLE = 3,
        SCREW = 4,
        UNKNOWN_SCREW_5 = 5,
        UNKNOWN_6 = 6,
        UNKNOWN_SCREW_7 = 7,
        GROUNDED_FACE_DOWN = 8,
        GROUNDED_FACE_UP = 9,
        COUNTER_HIT_STANDING = 10,
        COUNTER_HIT_CROUCHING = 11,
        NORMAL_HIT_STANDING = 12,
        NORMAL_HIT_CROUCHING = 13,
        NORMAL_HIT_STANDING_LEFT = 14,
        NORMAL_HIT_CROUCHING_LEFT = 15,
        NORMAL_HIT_STANDING_BACK = 16,
        NORMAL_HIT_CROUCHING_BACK = 17,
        NORMAL_HIT_STANDING_RIGHT = 18,
        NORMAL_HIT_CROUCHING_RIGHT = 19
    };

    // InputDirectionCodes
    enum class InputDirectionCodes: long long {
        _NULL = 0,
        N = 0x20,
        u = 0x100,
        ub = 0x80,
        uf = 0x200,
        f = 0x40,
        b = 0x10,
        d = 4,
        df = 8,
        db = 2
    };

    // InputAttackCodes
    enum class InputAttackCodes: long long {
        N = 0,
        x1 = 512,
        x2 = 1024,
        x3 = 2048,
        x4 = 4096,
        x1x2 = 1536,
        x1x3 = 2560,
        x1x4 = 4608,
        x2x3 = 3072,
        x2x4 = 5120,
        x3x4 = 6144,
        x1x2x3 = 3584,
        x1x2x4 = 5632,
        x1x3x4 = 6656,
        x2x3x4 = 7168,
        x1x2x3x4 = 7680,
        xRAGE = 8192
    };

    // CharacterCodes
    enum class CharacterCodes: long long {
        PAUL = 0,
        LAW = 1,
        KING = 2,
        YOSHIMITSU = 3,
        HWOARANG = 4,
        XIAOYU = 5,
        JIN = 6,
        BRYAN = 7,
        KAZUYA = 8,
        STEVE = 9,
        JACK = 10,
        ASUKA = 11,
        DEVIL_JIN = 12,
        FENG = 13,
        LILI = 14,
        DRAGUNOV = 15,
        LEO = 16,
        LARS = 17,
        ALISA = 18,
        CLAUDIO = 19,
        SHAHEEN = 20,
        NINA = 21,
        LEE = 22,
        KUMA = 23,
        PANDA = 24,
        ZAFINA = 25,
        LEROY = 26,
        JUN = 27,
        REINA = 28,
        AZUCENA = 29,
        VICTOR = 30,
        RAVEN = 31,
        EDDY = 33,
        LIDIA = 34,
        HEIHACHI = 35,
        CLIVE = 36,
        ANNA = 37,
        FAHKUMRAM = 38,
        ARMOR_KING = 39,
        MIARY_ZO = 40,
        _DUMMY = 116
    };

    template<>
    std::unordered_map<UniversalMoves, std::string>& SafeEnum<UniversalMoves>::string_map();

    template<>
    std::unordered_map<long long, AttackType>& SafeEnum<AttackType>::value_map();

    template<>
    std::unordered_map<AttackType, std::string>& SafeEnum<AttackType>::string_map();

    template<>
    std::unordered_map<long long, SimpleMoveStates>& SafeEnum<SimpleMoveStates>::value_map();

    template<>
    std::unordered_map<SimpleMoveStates, std::string>& SafeEnum<SimpleMoveStates>::string_map();

    template<>
    std::unordered_map<long long, ComplexMoveStates>& SafeEnum<ComplexMoveStates>::value_map();

    template<>
    std::unordered_map<ComplexMoveStates, std::string>& SafeEnum<ComplexMoveStates>::string_map();

    template<>
    std::unordered_map<long long, ThrowTechs>& SafeEnum<ThrowTechs>::value_map();

    template<>
    std::unordered_map<ThrowTechs, std::string>& SafeEnum<ThrowTechs>::string_map();

    template<>
    std::unordered_map<long long, StunStates>& SafeEnum<StunStates>::value_map();

    template<>
    std::unordered_map<StunStates, std::string>& SafeEnum<StunStates>::string_map();

    template<>
    std::unordered_map<long long, HitOutcome>& SafeEnum<HitOutcome>::value_map();

    template<>
    std::unordered_map<HitOutcome, std::string>& SafeEnum<HitOutcome>::string_map();

    template<>
    std::unordered_map<long long, InputDirectionCodes>& SafeEnum<InputDirectionCodes>::value_map();

    template<>
    std::unordered_map<InputDirectionCodes, std::string>& SafeEnum<InputDirectionCodes>::string_map();

    template<>
    std::unordered_map<long long, InputAttackCodes>& SafeEnum<InputAttackCodes>::value_map();

    template<>
    std::unordered_map<InputAttackCodes, std::string>& SafeEnum<InputAttackCodes>::string_map();

    template<>
    std::unordered_map<long long, CharacterCodes>& SafeEnum<CharacterCodes>::value_map();

    template<>
    std::unordered_map<CharacterCodes, std::string>& SafeEnum<CharacterCodes>::string_map();

}