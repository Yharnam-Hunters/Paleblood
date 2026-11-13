// SPDX-License-Identifier: GPL-2.0-or-later
// An enemy's AI thinking: the per-character think object the AI scripts read and write through
// their bindings (SprjAiFunc, the "ai:" methods), the goal-side context the goal scripts use
// (SprjGoalFunc), and the perception record that holds the character's target state. Fields are
// named for what the bindings show; `unknown_0x...` is space nobody has needed yet.
//
// The game's AI behaviour is written in script files that run on the script VM; these bindings
// are the engine side those scripts call. Only the engine side lives here.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace ai {

// Script timers count down; a timer at or below zero has finished. Indices above the last timer
// are ignored by the setters and read as zero (or as "finished" by the think's IsFinishTimer).
constexpr int think_timer_count = 16;
constexpr int last_think_timer = think_timer_count - 1;
// Script numbers: free per-character values the scripts keep between frames.
constexpr int think_number_count = 64;
constexpr unsigned last_think_number = think_number_count - 1;

// What the character knows about its target. The bindings compare the current state with these
// values (IsCautionState, IsFindState, IsBattleState); 0 is the state with no target noticed.
enum class TargetState : int32_t {
    normal = 0,
    caution = 1,
    find = 2,
    battle = 3,
};

// The perception record of a character's think (target state now and last frame, target type).
struct ThinkPerception {
    uint8_t unknown_0x00[0xc4];
    TargetState current_state;
    TargetState previous_state;
    int32_t current_target_type;
};
static_assert(offsetof(ThinkPerception, current_state) == 0xc4);
static_assert(offsetof(ThinkPerception, previous_state) == 0xc8);
static_assert(offsetof(ThinkPerception, current_target_type) == 0xcc);

// A forced target state of -1 means none: the perception's own state applies.
constexpr int32_t no_forced_target_state = -1;

// One character's think (the object the "ai:" bindings act on).
struct AiThink {
    uint8_t unknown_0x000[0x28];
    int32_t think_param_id;   // the NpcThinkParam row in use
    uint8_t unknown_0x02c[0x50];
    float timers[think_timer_count];
    float numbers[think_number_count];
    uint8_t unknown_0x1bc[0x3a4];
    ThinkPerception *perception;
    uint8_t unknown_0x568[0x440];
    int32_t forced_target_state;
};
static_assert(offsetof(AiThink, think_param_id) == 0x28);
static_assert(offsetof(AiThink, timers) == 0x7c && offsetof(AiThink, numbers) == 0xbc);
static_assert(offsetof(AiThink, perception) == 0x560);
static_assert(offsetof(AiThink, forced_target_state) == 0x9a8);

// The binding object behind the scripts' "ai:" methods (SprjAiFunc): its think.
struct AiFunc {
    uint8_t unknown_0x00[0x8];
    AiThink *think;
};
static_assert(offsetof(AiFunc, think) == 0x8);

// The goal scripts' own timers and numbers (a separate set from the think's), and the timer
// SetTimerForTurnBeforeAtk sets.
struct GoalContext {
    uint8_t unknown_0x000[0xb8];
    float timers[think_timer_count];
    float numbers[think_number_count];
    uint8_t unknown_0x1f8[0x18];
    float turn_before_attack_timer;
};
static_assert(offsetof(GoalContext, timers) == 0xb8 && offsetof(GoalContext, numbers) == 0xf8);
static_assert(offsetof(GoalContext, turn_before_attack_timer) == 0x210);

// The binding object behind the goal scripts' methods (SprjGoalFunc): its goal context.
struct GoalFunc {
    uint8_t unknown_0x00[0x8];
    GoalContext *context;
};
static_assert(offsetof(GoalFunc, context) == 0x8);

// A character as the bindings see it: its stats through virtual calls.
struct AiCharacter;
struct AiCharacterVtable {
    void *unknown_slots[11];
    int32_t (*hp)(AiCharacter *);
    void *unknown_slot_0x60;
    int32_t (*mp)(AiCharacter *);
    int32_t (*sp)(AiCharacter *);
    float (*hp_rate)(AiCharacter *);
};
static_assert(offsetof(AiCharacterVtable, hp) == 0x58 && offsetof(AiCharacterVtable, mp) == 0x68);
static_assert(offsetof(AiCharacterVtable, sp) == 0x70 && offsetof(AiCharacterVtable, hp_rate) == 0x78);
struct AiCharacter {
    const AiCharacterVtable *vtable;
};

// The engine's random number source the AI scripts draw from.
struct RandomSource;
struct RandomSourceVtable {
    void *unknown_slots[2];
    uint32_t (*next)(RandomSource *);
};
static_assert(offsetof(RandomSourceVtable, next) == 0x10);
struct RandomSource {
    const RandomSourceVtable *vtable;
};

// The game object that owns that random source (the camera code reads other fields of the same
// singleton: game/camera/follow_camera_state.h, parameter_overrides).
struct RandomSourceOwner {
    uint8_t unknown_0x000[0xa60];
    RandomSource random;
};
static_assert(offsetof(RandomSourceOwner, random) == 0xa60);

// A random draw keeps its low 20 bits and scales them by 2^-20 into [0, 1).
constexpr uint32_t random_draw_mask = 0xfffff;
constexpr float random_draw_scale = 1.0f / (1 << 20);

RT_GLOBAL(0x05956678, random_source_owner, RandomSourceOwner *);

// The character a script refers to (`target` is the script's target selector: itself, its
// target, ...), or null.
RT_ORIGINAL(0x01691e10, ai_find_character, AiCharacter *(AiThink *, int32_t));

}  // namespace ai
