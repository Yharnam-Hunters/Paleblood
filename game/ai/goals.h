// SPDX-License-Identifier: GPL-2.0-or-later
// AI goals: the objects an enemy's AI plans with, and the engine code around them.
//
// An enemy's think object keeps a pool of goals. Its scripts add goals by id (AddTopGoal on the
// root goal, AddSubGoal on the goal that is running), each with a life in seconds (-1: no limit)
// and up to fifteen float parameters. Every goal id has a definition, registered when the AI
// scripts load: the script functions it runs (Activate, Update, Terminate, Interrupt) and options
// the scripts set with REGISTER_GOAL_NO_UPDATE, REGISTER_GOAL_UPDATE_TIME,
// REGISTER_GOAL_NO_INTERUPT and ENABLE_COMBO_ATK_CANCEL. The goal update runs a goal's script
// Update at a random interval within the definition's update-time range.
//
// Names follow the game's own script binding names; the layouts come from the disassembly.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace ai {

// What a goal id's registration options set. The rest of the definition (its name and the
// script functions it calls) is not modelled yet.
struct GoalDefinition {
    uint8_t unknown_0x000[0x124];
    uint8_t no_update;          // REGISTER_GOAL_NO_UPDATE
    uint8_t no_interrupt;       // REGISTER_GOAL_NO_INTERUPT
    uint8_t unknown_0x126[0x2];
    float update_time_min;      // REGISTER_GOAL_UPDATE_TIME: the script Update's interval range,
    float update_time_max;      // in seconds
    uint8_t unknown_0x130;
    uint8_t combo_attack_cancel;   // ENABLE_COMBO_ATK_CANCEL
};
static_assert(offsetof(GoalDefinition, no_update) == 0x124 && offsetof(GoalDefinition, no_interrupt) == 0x125);
static_assert(offsetof(GoalDefinition, update_time_min) == 0x128 && offsetof(GoalDefinition, update_time_max) == 0x12c);
static_assert(offsetof(GoalDefinition, combo_attack_cancel) == 0x131);

// The world AI manager (SprjWorldAiManager): it owns the goal definitions, among much else.
struct WorldAiManager;

// Spreads AI updates over frames (SprjAiUpdateTimeSchedular, the game's spelling): a ring of 100
// one-frame slots counting the updates reserved in each. It sits inside the target bank manager.
constexpr int scheduler_slot_count = 100;
struct AiUpdateTimeScheduler {
    uint8_t reservations[scheduler_slot_count];
    int32_t cursor;             // the slot of the current frame
    uint8_t limit_per_slot;     // more reservations than this move to a later slot
    uint8_t disabled;           // reserve without the limit
};
static_assert(offsetof(AiUpdateTimeScheduler, cursor) == 0x64);
static_assert(offsetof(AiUpdateTimeScheduler, limit_per_slot) == 0x68 && offsetof(AiUpdateTimeScheduler, disabled) == 0x69);

RT_GLOBAL(0x0593b0a0, world_ai_manager_instance, WorldAiManager *);
RT_GLOBAL(0x04d35687, world_ai_manager_name, const char);

// The definition registered for a goal id, or null.
RT_ORIGINAL(0x016dda50, goal_definition_find, GoalDefinition *(WorldAiManager *, int32_t goal_id));

// A goal: one slot of a think's goal pool (32 slots of 0x3e8 bytes; a free slot's id is -1). It
// keeps its owner, the queue of sub-goals it runs one after another, its life, its parameters
// and its script timers and numbers (GoalContext in ai_think.h views the same object). Not
// modelled yet: the bindings only pass goals on.
struct Goal;
// What the scripts hold for a goal: the object 0x18 bytes into it, which the goal-adding
// functions return.
struct GoalScriptObject;

// The goal side of a character's think: its goal pool and the root goal that AddTopGoal adds
// under. This is the same object as AiThink (ai_think.h); its goal fields are named here, next
// to the goal code, so that the think's other users keep their header unchanged.
struct ThinkGoals {
    uint8_t unknown_0x00[0x50];
    Goal *pool;           // 32 goals
    uint8_t unknown_0x58[0x8];
    Goal *root_goal;
};
static_assert(offsetof(ThinkGoals, pool) == 0x50 && offsetof(ThinkGoals, root_goal) == 0x60);

// A goal takes up to 15 float parameters; the scripts read them back with GetParam.
constexpr int goal_parameter_count = 15;

// The goal-adding core: takes the first free slot of the owner's think's goal pool for
// `goal_id`, sets its life (seconds; -1: no limit) and parameters, and queues it at the back of
// `owner`'s sub-goals (goal_add_front: at the front, so it runs next). Returns the new goal's
// script object, or null when the id is negative or the pool is full. Parameters 1..5 are kept
// as they are; 6..15 go into a list as long as the last one that is not zero.
RT_ORIGINAL(0x0164cc60, goal_add,
            GoalScriptObject *(Goal *owner, int32_t goal_id, float life, float, float, float, float, float, float,
                               float, float, float, float, float, float, float, float, float));
RT_ORIGINAL(0x0164d4f0, goal_add_front,
            GoalScriptObject *(Goal *owner, int32_t goal_id, float life, float, float, float, float, float, float,
                               float, float, float, float, float, float, float, float, float));

}  // namespace ai
