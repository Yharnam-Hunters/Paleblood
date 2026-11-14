// SPDX-License-Identifier: GPL-2.0-or-later
// AI goals (goals.h): the update-time scheduler's reserve and the goal registration options.
//
// - ai_update_time_scheduler_reserve: books one update `delay` seconds ahead. The delay becomes a
//   frame count at 30 frames a second, counted from the cursor around the 100-slot ring. A slot
//   below its limit (or any slot while the scheduler is disabled) takes the booking and the delay
//   comes back unchanged. A full slot passes it on to the next slots, one frame later each; the
//   first one below the limit takes it and the delay comes back moved to that frame. When all
//   100 are full nothing is booked and the delay comes back 100 frames later. Counts are bytes
//   and wrap. No call to it is in the executable; the target bank manager owns the scheduler.
// - The registration options: script functions the AI scripts call at load for a goal id. Each
//   requires the world AI manager (reporting a missing one and reading it again, as the game
//   does), finds the id's definition and, when there is one, sets one option.
#include "goals.h"

#include <cstdint>

#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "../engine/truncate_64.h"

namespace ai {
namespace {

// 30.0 in read-only data: the scheduler's frames per second.
RT_GLOBAL(0x04d2576c, scheduler_frames_per_second, const float);

WorldAiManager *require_world_ai_manager()
{
    return engine::require(world_ai_manager_instance, world_ai_manager_name.address());
}

GoalDefinition *find_definition(int32_t goal_id)
{
    return goal_definition_find(require_world_ai_manager(), goal_id);
}

// The delay moved `frames` later.
float delayed_by_frames(float delay, int32_t frames)
{
    return engine::add(engine::divide(static_cast<float>(frames), scheduler_frames_per_second.get()), delay);
}

}  // namespace
}  // namespace ai

using namespace ai;

extern "C" float bb_ai_update_time_scheduler_reserve(AiUpdateTimeScheduler *scheduler, float delay)
{
    const float frames_per_second = scheduler_frames_per_second.get();
    // Only the low 32 bits of the truncated frame count are used, unsigned: a delay out of
    // range (or NaN) counts as frame 0, and a negative one wraps.
    const auto frames = static_cast<uint32_t>(engine::truncate_64(engine::multiply(delay, frames_per_second)));
    uint32_t slot = (frames + static_cast<uint32_t>(scheduler->cursor)) % scheduler_slot_count;
    uint8_t &first = scheduler->reservations[slot];
    if (scheduler->disabled || first < scheduler->limit_per_slot) {
        first++;
        return delay;
    }
    // The next 99 slots, then the first one again (still full: the game checks it a second time).
    for (int32_t later = 1;; later++) {
        slot = (slot + 1) % scheduler_slot_count;
        uint8_t &count = scheduler->reservations[slot];
        if (count < scheduler->limit_per_slot) {
            count++;
            return delayed_by_frames(delay, later);
        }
        if (later == scheduler_slot_count)
            return delayed_by_frames(delay, later);
    }
}

extern "C" void bb_ai_func_register_goal_no_update(int32_t goal_id, uint8_t no_update)
{
    if (GoalDefinition *definition = find_definition(goal_id))
        definition->no_update = no_update;
}

extern "C" void bb_ai_func_register_goal_update_time(int32_t goal_id, float min_seconds, float max_seconds)
{
    if (GoalDefinition *definition = find_definition(goal_id)) {
        definition->update_time_min = min_seconds;
        definition->update_time_max = max_seconds;
    }
}

extern "C" void bb_ai_func_register_goal_no_interrupt(int32_t goal_id, uint8_t no_interrupt)
{
    if (GoalDefinition *definition = find_definition(goal_id))
        definition->no_interrupt = no_interrupt;
}

extern "C" void bb_ai_func_enable_combo_attack_cancel(int32_t goal_id)
{
    if (GoalDefinition *definition = find_definition(goal_id))
        definition->combo_attack_cancel = 1;
}
