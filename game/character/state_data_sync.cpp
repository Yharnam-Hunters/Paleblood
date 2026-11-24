// SPDX-License-Identifier: GPL-2.0-or-later
// Character: synchronizes ChrStateData from modules and active effects (0x01cf7880).
//
// Called from the per-frame character update (0x01cf4ff0) when the character is the main player
// or passes vtable+0x1c0, and tail-called from 0x01cfd460.
//
// Non-obvious behavior:
// - Clamps points values against ChrStateData's prior limits before copying the new limits.
// - Zeroes active counters with quiet scalar `> 0.0f` checks (vucomiss, leaving MXCSR invalid
//   clear on quiet NaNs) while copying un-zeroed raw counters with slots 1 and 2 swapped.
// - Reloads self->effects after associated-object sync only when assoc_first is non-null, and
//   passes every node in the final list to 0x01bec080 without filtering inactive flags.
#include "state_data_sync.h"

#include <cstdint>

#include "../engine/scalar.h"
#include "../engine/scalar_compare.h"

namespace {

using namespace character::state_data_sync;

int32_t clamp_to_limit(int32_t value, int32_t limit) { return limit > value ? value : limit; }

const EffectNode *find_active_effect(const EffectList *effects, uint32_t mask, uint32_t required_bits,
                                     uint16_t target_type)
{
    if ((mask & required_bits) == 0) return nullptr;
    for (const EffectNode *node = effects->head; node != nullptr; node = node->next) {
        if ((node->flags & effect_inactive_mask) != 0) continue;
        if (!node->param) continue;
        if (node->param->type_0x156 == target_type) return node;
    }
    return nullptr;
}

bool has_special_effect(const EffectList *effects, uint32_t mask)
{
    return find_active_effect(effects, mask, mask_type_0x88, effect_type_0x88) != nullptr ||
           find_active_effect(effects, mask, mask_type_0x75, effect_type_0x75) != nullptr ||
           find_active_effect(effects, mask, mask_type_0x74, effect_type_0x74) != nullptr;
}

float sum_type_0x9b_bonuses(const EffectList *effects)
{
    float bonus = 0.0f;
    if ((effects->active_mask & mask_type_0x9b) == 0) return bonus;
    for (const EffectNode *node = effects->head; node != nullptr; node = node->next) {
        if ((node->flags & effect_inactive_mask) != 0) continue;
        if (!node->param || node->param->type_0x156 != effect_type_0x9b) continue;
        const float term = static_cast<float>(static_cast<int32_t>(node->param->bonus_0x13a));
        bonus = engine::add(bonus, term);
    }
    return bonus;
}

}  // namespace

extern "C" void bb_character_state_data_sync(ChrIns *self)
{
    ChrStateData *state = self->vtable->state_data(self);
    if (!state) return;

    // Clamp each points value against the state object's existing limit before updating limits.
    const ChrPointsData *points = self->modules->points;
    state->value_0x14 = clamp_to_limit(points->value_0xf8, state->limit_0x18);
    state->value_0x20 = clamp_to_limit(points->value_0x128, state->limit_0x24);
    state->value_0x30 = clamp_to_limit(points->value_0x134, state->limit_0x34);

    const EventGate *gate = self->gate_holder->gate;
    if (gate && (gate->flags & event_gate_active_bit))
        state->flags_0x650 |= state_flag_gate_bit;
    else
        state->flags_0x650 &= static_cast<uint8_t>(~state_flag_gate_bit);

    points = self->modules->points;
    state->limit_0x18 = points->limit_0xfc;
    state->limit_0x24 = points->limit_0x12c;
    state->limit_0x34 = points->limit_0x138;
    state->cap_0x5f4 = state_cap_sentinel;

    state_sync_step_01cfcaf0(self);
    if (self->vtable->skip_full_state_sync(self) != 0) return;

    state_sync_step_01cfccb0(self);
    state_sync_step_01cfd110(self);

    // Reload self->effects and self->modules->counters after the helper calls above.
    const EffectList *effects = self->effects;
    CounterModule *counters = self->modules->counters;
    int32_t active_0 = counters->counters[counter_slot_0];
    int32_t active_1 = counters->counters[counter_slot_1];
    int32_t active_2 = counters->counters[counter_slot_2];
    int32_t active_3 = counters->counters[counter_slot_3];
    int32_t active_4 = counters->counters[counter_slot_4];

    const uint32_t mask = effects->active_mask;
    const bool special = has_special_effect(effects, mask);
    const float val_a_3 = special ? effect_special_value_a : effect_absent_value;
    const float val_b_3 = special ? effect_special_value_b : effect_absent_value;

    const EffectNode *node_2 = find_active_effect(effects, mask, mask_type_2, effect_type_2);
    const EffectNode *node_5 = find_active_effect(effects, mask, mask_type_5, effect_type_5);
    const EffectNode *node_6 = find_active_effect(effects, mask, mask_type_6, effect_type_6);
    const EffectNode *node_104 = find_active_effect(effects, mask, mask_type_0x104, effect_type_0x104);

    const float val_a_0 = node_2 ? node_2->value_0x00 : effect_absent_value;
    const float val_a_1 = node_5 ? node_5->value_0x00 : effect_absent_value;
    const float val_a_2 = node_6 ? node_6->value_0x00 : effect_absent_value;
    const float val_104 = node_104 ? node_104->value_0x00 : effect_absent_value;

    const float val_b_0 = node_2 ? node_2->value_0x04 : effect_absent_value;
    const float val_b_1 = node_5 ? node_5->value_0x04 : effect_absent_value;
    const float val_b_2 = node_6 ? node_6->value_0x04 : effect_absent_value;

    // Quiet scalar > 0.0f (vucomiss + cmova): preserves NaN payloads and avoids signaling invalid.
    if (engine::quiet_greater(val_a_0, 0.0f)) active_0 = 0;
    if (engine::quiet_greater(val_a_1, 0.0f)) active_1 = 0;
    if (engine::quiet_greater(val_a_2, 0.0f)) active_2 = 0;
    if (engine::quiet_greater(val_a_3, 0.0f)) active_3 = 0;
    if (engine::quiet_greater(val_104, 0.0f)) active_4 = 0;

    // Original copies bits 0..3 individually (reloading counters after each byte store due to
    // strict aliasing); with no intervening callback and distinct game objects, copy the nibble.
    const uint8_t counter_flags = static_cast<uint8_t>(counters->flags_0x4c & state_flag_counter_mask);
    state->flags_0x650 =
        static_cast<uint8_t>((state->flags_0x650 & static_cast<uint8_t>(~state_flag_counter_mask)) | counter_flags);
    counters->flags_0x4c = 0;

    state->active_counters[counter_slot_0] = active_0;
    state->active_counters[counter_slot_1] = active_1;
    state->active_counters[counter_slot_2] = active_2;
    state->active_counters[counter_slot_3] = active_3;
    state->active_counters[counter_slot_4] = active_4;

    // Raw counters keep the un-zeroed values, with slots 1 and 2 swapped in ChrStateData +0x104/+0x108.
    state->raw_counters[counter_slot_0] = counters->counters[counter_slot_0];
    state->raw_counters[counter_slot_1] = counters->counters[counter_slot_2];
    state->raw_counters[counter_slot_2] = counters->counters[counter_slot_1];
    state->raw_counters[counter_slot_3] = counters->counters[counter_slot_3];
    state->raw_counters[counter_slot_4] = counters->counters[counter_slot_4];

    state->effect_values_a[effect_slot_0] = val_a_0;
    state->effect_values_a[effect_slot_1] = val_a_1;
    state->effect_values_a[effect_slot_2] = val_a_2;
    state->effect_values_a[effect_slot_3] = val_a_3;

    state->effect_values_b[effect_slot_0] = val_b_0;
    state->effect_values_b[effect_slot_1] = val_b_1;
    state->effect_values_b[effect_slot_2] = val_b_2;
    state->effect_values_b[effect_slot_3] = val_b_3;

    state->counter_maxima[counter_slot_0] = counters->maxima[counter_slot_0];
    state->counter_maxima[counter_slot_1] = counters->maxima[counter_slot_1];
    state->counter_maxima[counter_slot_2] = counters->maxima[counter_slot_2];
    state->counter_maxima[counter_slot_3] = counters->maxima[counter_slot_3];
    state->counter_maxima[counter_slot_4] = counters->maxima[counter_slot_4];

    const float base_stat = self->vtable->base_stat_0x2b8(self);
    EffectList *live_effects = self->effects;
    const float bonus = sum_type_0x9b_bonuses(live_effects);
    state->stat_0x668 = engine::truncate(engine::add(base_stat, bonus));

    state->effect_mode_0x66c = query_effect_mode_01979450(live_effects);
    state->cap_0x604 = state_cap_sentinel;

    if (AssociatedObject *first = state->assoc_first) {
        sync_associated_effects_01be1860(first, live_effects);
        first->reset_0x28 = associated_reset_value;

        AssociatedObject *second = state->assoc_second;
        sync_associated_effects_01be1860(second, live_effects);
        second->reset_0x28 = associated_reset_value;

        // Original reloads self->effects only after updating the associated objects.
        live_effects = self->effects;
    }

    for (EffectNode *node = live_effects->head; node != nullptr; node = node->next)
        process_effect_node_01bec080(node, state);
}
