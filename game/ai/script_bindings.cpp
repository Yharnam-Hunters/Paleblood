// SPDX-License-Identifier: GPL-2.0-or-later
// AI: script bindings. The game's enemy behaviour is written in AI scripts; each frame a
// character's logic and goal scripts call back into the engine through methods registered on the
// script VM under the names the scripts use ("GetRandam_Int", "SetTimer", "IsBattleState", ...;
// the registration code runs at 0x01640000-0x016b0000). This file holds the small ones that read
// or change a character's think: random draws, the script timers and numbers, the target state
// (normal, caution, find, battle) and stats.
//
// Two binding objects exist: SprjAiFunc for the logic scripts ("ai:" methods, on the character's
// think) and SprjGoalFunc for the goal scripts, which keep their own timers and numbers.
//
// Index checks follow the originals exactly. The timer indices are compared signed (a negative
// index passes the check and reaches the memory before the array, as in the original), the number
// indices unsigned (a negative index is refused). Out of range, the think's IsFinishTimer answers
// true but the goal's answers false.
#include <cstdint>

#include "../engine/scalar.h"
#include "ai_think.h"

namespace ai {
namespace {

// One draw from the engine's random source, scaled into [0, 1) with 20 bits.
float random_fraction()
{
    RandomSource *random = &random_source_owner.get()->random;
    return static_cast<float>(static_cast<int32_t>(random->vtable->next(random) & random_draw_mask)) *
           random_draw_scale;
}

// The timer at `index` of `timers`, which holds `think_timer_count` of them; only the upper bound
// is checked (signed), so a negative index addresses memory before the array.
float *timer_at(float *timers, int32_t index) { return timers + index; }

bool timer_finished(float timer) { return timer <= 0.0f; }

TargetState state_of(const AiFunc *func) { return func->think->perception->current_state; }

}  // namespace
}  // namespace ai

using namespace ai;

// GetRandam_Float(min, max): min + (max - min) * fraction.
extern "C" float bb_ai_func_get_random_float(AiFunc *, float min, float max)
{
    const float fraction = random_fraction();
    return engine::add(engine::multiply(engine::subtract(max, min), fraction), min);
}

// GetRandam_Int(min, max): min + (max - min + 1) * fraction, truncated: an integer in [min, max].
// The difference wraps as a 32-bit integer.
extern "C" int32_t bb_ai_func_get_random_int(AiFunc *, int32_t min, int32_t max)
{
    const float fraction = random_fraction();
    const int32_t span = static_cast<int32_t>(static_cast<uint32_t>(max) - static_cast<uint32_t>(min));
    return engine::truncate(static_cast<float>(min) + (static_cast<float>(span) + 1.0f) * fraction);
}

extern "C" void bb_ai_func_set_timer(AiFunc *func, int32_t index, float seconds)
{
    if (index <= last_think_timer) *timer_at(func->think->timers, index) = seconds;
}

extern "C" void bb_ai_func_set_number(AiFunc *func, uint32_t index, float value)
{
    if (index <= last_think_number) func->think->numbers[index] = value;
}

extern "C" float bb_ai_func_get_timer(AiFunc *func, int32_t index)
{
    return index <= last_think_timer ? *timer_at(func->think->timers, index) : 0.0f;
}

extern "C" bool bb_ai_func_is_finish_timer(AiFunc *func, int32_t index)
{
    return index <= last_think_timer ? timer_finished(*timer_at(func->think->timers, index)) : true;
}

extern "C" float bb_ai_func_get_number(AiFunc *func, uint32_t index)
{
    return index <= last_think_number ? func->think->numbers[index] : 0.0f;
}

extern "C" bool bb_ai_func_is_battle_state(AiFunc *func) { return state_of(func) == TargetState::battle; }

// True on the frame the target state changed.
extern "C" bool bb_ai_func_is_change_state(AiFunc *func)
{
    const ThinkPerception *perception = func->think->perception;
    return perception->current_state != perception->previous_state;
}

extern "C" bool bb_ai_func_is_caution_state(AiFunc *func) { return state_of(func) == TargetState::caution; }

extern "C" bool bb_ai_func_is_find_state(AiFunc *func) { return state_of(func) == TargetState::find; }

// The forced target state when one is set, else the perception's.
extern "C" int32_t bb_ai_func_get_current_target_state(AiFunc *func)
{
    const AiThink *think = func->think;
    if (think->forced_target_state != no_forced_target_state) return think->forced_target_state;
    return static_cast<int32_t>(think->perception->current_state);
}

extern "C" int32_t bb_ai_func_get_previous_target_state(AiFunc *func)
{
    return static_cast<int32_t>(func->think->perception->previous_state);
}

// The stat getters ask the character the script names; with no such character they answer 0.
extern "C" int32_t bb_ai_func_get_sp(AiFunc *func, int32_t target)
{
    AiCharacter *character = ai_find_character(func->think, target);
    return character ? character->vtable->sp(character) : 0;
}

extern "C" int32_t bb_ai_func_get_mp(AiFunc *func, int32_t target)
{
    AiCharacter *character = ai_find_character(func->think, target);
    return character ? character->vtable->mp(character) : 0;
}

extern "C" int32_t bb_ai_func_get_hp(AiFunc *func, int32_t target)
{
    AiCharacter *character = ai_find_character(func->think, target);
    return character ? character->vtable->hp(character) : 0;
}

extern "C" float bb_ai_func_get_hp_rate(AiFunc *func, int32_t target)
{
    AiCharacter *character = ai_find_character(func->think, target);
    return character ? character->vtable->hp_rate(character) : 0.0f;
}

extern "C" void bb_ai_goal_func_set_timer(GoalFunc *func, int32_t index, float seconds)
{
    if (index <= last_think_timer) *timer_at(func->context->timers, index) = seconds;
}

extern "C" void bb_ai_goal_func_set_number(GoalFunc *func, uint32_t index, float value)
{
    if (index <= last_think_number) func->context->numbers[index] = value;
}

extern "C" float bb_ai_goal_func_get_timer(GoalFunc *func, int32_t index)
{
    return index <= last_think_timer ? *timer_at(func->context->timers, index) : 0.0f;
}

extern "C" bool bb_ai_goal_func_is_finish_timer(GoalFunc *func, int32_t index)
{
    return index <= last_think_timer ? timer_finished(*timer_at(func->context->timers, index)) : false;
}

// IsFinishTimerForTurnBeforeAtk: the timer SetTimerForTurnBeforeAtk set has run out.
extern "C" bool bb_ai_goal_func_is_finish_turn_timer(GoalFunc *func)
{
    return timer_finished(func->context->turn_before_attack_timer);
}

extern "C" float bb_ai_goal_func_get_number(GoalFunc *func, uint32_t index)
{
    return index <= last_think_number ? func->context->numbers[index] : 0.0f;
}
