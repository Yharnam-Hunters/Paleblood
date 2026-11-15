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

}  // namespace ai
