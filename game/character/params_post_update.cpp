// SPDX-License-Identifier: GPL-2.0-or-later
// Character: post-update of ChrParams from the active DrawTarget (0x0190a5f0).
//
// Tail-called from character_data_update_01913c10 (0x0191430d) with (self->params, active_target);
// the caller leaves frame_time in xmm0, which this callee never reads.
//
// When anim_events is ready, mirrors visible DrawTarget flags/state into ChrParams, ChrIns, and
// ChrFlagsModule; otherwise (gated or hidden with action state), propagates action_id, triggers
// throw_request_check when required, and updates the owner's packed flag bit. Re-reads
// self->data->owner (and target->flags_0x90 on the visible path) after virtual calls and
// throw_request_check to match the original's post-call reloads.
#include "params_post_update.h"

#include <cstdint>

namespace {

using namespace character::params_post_update;

void sync_visible_target(ChrParams *self, DrawTarget *target)
{
    if (target->vtable->sync_flags_ready(target) != 0) {
        const uint32_t target_flags = target->flags_0x90;
        ChrIns *owner = self->data->owner;

        if (target_flags & target_flag_bit0)
            self->flags_0x5c |= params_flags_0x5c_from_bit0;
        else
            self->flags_0x5c &= static_cast<uint16_t>(~params_flags_0x5c_from_bit0);

        if (target_flags & target_flag_bit1)
            owner->flags_0x1e4 |= chr_flags_0x1e4_bit;
        else
            owner->flags_0x1e4 &= static_cast<uint8_t>(~chr_flags_0x1e4_bit);

        if (target_flags & target_flag_bit3)
            self->flags_0x5c |= params_flags_0x5c_from_bit3;
        else
            self->flags_0x5c &= static_cast<uint16_t>(~params_flags_0x5c_from_bit3);

        const uint32_t raw_state = target_flags >> target_state_shift;
        const int32_t next_state = static_cast<int32_t>(raw_state) - 1;
        if (owner->state_0x1c4 != next_state && raw_state != 0) owner->vtable->on_state_change(owner, next_state);
        owner->state_0x1c4 = next_state;
    }

    // Re-read target->flags_0x90 and self->data->owner->modules->flags_module after any virtual calls.
    ChrFlagsModule *flags_module = self->data->owner->modules->flags_module;
    if (target->flags_0x90 & target_flag_bit4)
        flags_module->flags_0x818 |= flags_0x818_sync_bit;
    else
        flags_module->flags_0x818 &= static_cast<uint8_t>(~flags_0x818_sync_bit);
}

void handle_hidden_or_gated_target(ChrParams *self, DrawTarget *target)
{
    if (target->vtable->has_action_state(target) == 0) return;

    ChrIns *owner = self->data->owner;
    ChrModules *modules = owner->modules;

    // Original branches on action_request->bits_0x20 / bits_0x30 all converge onto the same
    // points > 0 gate; only points > 0 determines whether the action-request block executes.
    if (modules->points->points > 0) {
        ActionRequestModule *action_request = modules->action_request;
        const uint32_t action_id = action_request->action_id_0x94;
        self->action_id_0xa0 = action_id;

        const ChrFlagsModule *request_flags = action_request->owner->modules->flags_module;
        const uint64_t action_bits = (request_flags->flags_0x10 & flags_select_primary_bits) ? action_request->bits_0x20
                                                                                             : action_request->bits_0x30;
        const bool has_primary = (action_bits & action_bit_primary) != 0;
        const bool has_secondary = (action_bits & action_bit_secondary) != 0;
        if (has_primary || has_secondary || action_id == action_id_force_throw_check) {
            const uint32_t mode =
                (!has_primary && has_secondary) ? throw_check_mode_secondary : throw_check_mode_primary;
            throw_request_check(modules->throw_request, mode, 0);
            owner = self->data->owner;
            modules = owner->modules;
        }
    }

    const uint8_t mod_flags = modules->flags_module->flags_0x10;
    if (!(mod_flags & flags_update_owner_bit10)) return;

    bool enable = (mod_flags & flags_owner_bit10_value) != 0;
    if (enable) {
        const ChrData *owner_data = owner->data;
        const ChrParams *owner_params = owner_data ? owner_data->params : nullptr;
        if (owner_params && (owner_params->flags_0x42 & params_suppress_owner_bit10)) enable = false;
    }
    if (enable)
        owner->flags_0x1e2 |= chr_flags_0x1e2_bit;
    else
        owner->flags_0x1e2 &= static_cast<uint8_t>(~chr_flags_0x1e2_bit);
}

}  // namespace

extern "C" void bb_character_params_post_update(ChrParams *self, DrawTarget *target)
{
    ChrIns *owner = self->data->owner;
    const AnimEvents *events = owner->modules->anim_events;
    if (!events || !events->ready) return;

    const EventGate *gate = owner->gate_holder->gate;
    if (!(gate && (gate->flags & gate_suppress)) && target->vtable->visible(target) != 0)
        sync_visible_target(self, target);
    else
        handle_hidden_or_gated_target(self, target);
}
