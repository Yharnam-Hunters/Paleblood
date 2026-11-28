// SPDX-License-Identifier: GPL-2.0-or-later
// Character: per-frame motion and target update of ChrData (0x01913c10).
//
// Called unconditionally before normal return in character_frame_update_01cbdb20 (0x01cbded1).
// When the owner's animation events are ready, prepares step vectors and motion-enable flags
// (including yaw quantization and freeze handling) and calls ChrData::apply_step; always steps
// the active target; then, if animation events remain ready, transforms and optionally
// XZ-projects the target's local motion through the motion receiver, stores the result, and
// delegates parameter post-update to 0x0190a5f0.
#include "data_update.h"

#include <bit>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "../engine/vector_operations.h"

namespace {

using namespace character::data_update;
namespace ops = engine::vector_operations;

constexpr uint32_t float_magnitude_mask = 0x7fffffffu;
constexpr uint32_t float_infinity_bits = 0x7f800000u;

DrawTarget *active_target(const ChrData *self)
{
    return self->target ? self->target : self->fallback_target;
}

bool anim_events_ready(const ChrIns *owner)
{
    const AnimEvents *events = owner->modules->anim_events;
    return events && events->ready;
}

// Quantizes the rotation's yaw (y lane) to +/-pi, +/-pi/2, or +/-pi/4 when the scaled angular
// rate in degrees exceeds 100, 60, or 30; sign follows the original y lane, not the scaled rate.
engine::Vec4 quantize_yaw(ChrData *self, engine::Vec4 rotation)
{
    const float yaw = rotation.y();
    const int32_t scale = self->vtable->turn_rate_scale(self);
    const float scaled = engine::multiply(static_cast<float>(scale), yaw);
    const float abs_deg = ops::absolute(engine::multiply(scaled, rad_to_deg.get()));

    if (ops::ordered_greater(abs_deg, yaw_threshold_high.get())) {
        const float step = ops::ordered_greater(yaw, 0.0f) ? yaw_step_pos_pi.get() : yaw_step_neg_pi.get();
        return ops::with_y(rotation, step);
    }
    if (ops::ordered_greater(abs_deg, yaw_threshold_mid.get())) {
        const float step = ops::ordered_greater(yaw, 0.0f) ? yaw_step_pos_half_pi.get() : yaw_step_neg_half_pi.get();
        return ops::with_y(rotation, step);
    }
    if (ops::ordered_greater(abs_deg, yaw_threshold_low.get())) {
        const float step =
            ops::ordered_greater(yaw, 0.0f) ? yaw_step_pos_quarter_pi.get() : yaw_step_neg_quarter_pi.get();
        return ops::with_y(rotation, step);
    }
    return rotation;
}

// Reproduces the original's up-to-three virtual calls to owner->vtable->linked(owner):
// blocks when the second call has no source or state 0, or when the third call has state 10.
bool linked_blocks_motion(ChrData *self, ChrIns *owner)
{
    if (!owner || !owner->vtable->linked(owner)) return false;
    const LinkedSource *first = self->owner->vtable->linked(self->owner)->source;
    if (!first || first->state_0x130 == 0) return true;
    const LinkedSource *second = self->owner->vtable->linked(self->owner)->source;
    return second && second->state_0x130 == linked_state_blocked;
}

bool should_freeze_motion(ChrData *self, const ChrModules *modules)
{
    if (modules->points->flags & points_freeze_motion) return true;
    if (!world_chr_man_dbg_instance.get()) engine::report_missing(world_chr_man_dbg_name.address());
    if (!debug_freeze_motion.get()) return false;
    if (self->owner->vtable->debug_freeze_exempt_a(self->owner)) return false;
    if (self->owner->vtable->debug_freeze_exempt_b(self->owner)) return false;
    return true;
}

float dot4_horizontal(engine::Vec4 v, engine::Vec4 row)
{
    const engine::Vec4 product = ops::multiply_in_order(v, row);
    return ops::horizontal_add(ops::horizontal_add(product)).x();
}

engine::Vec4 project_to_horizontal(float rx, float ry, float rz)
{
    const float rx2 = engine::multiply(rx, rx);
    const float rz2 = engine::multiply(rz, rz);
    const float xz_sq = engine::add(engine::add(rx2, 0.0f), rz2);
    const float xz_len = ops::square_root(xz_sq);
    if (!ops::ordered_greater(xz_len, horizontal_length_epsilon.get())) return vector_zero.get();

    const float ry2 = engine::multiply(ry, ry);
    const float xyz_sq = engine::add(engine::add(rx2, ry2), rz2);
    const float xyz_len = ops::square_root(xyz_sq);

    float nx;
    float ny;
    float nz;
    // Retained from the original's inlined vector normalization (0x019141e2..0x019141f1):
    // unreachable in IEEE-754 behind the preceding xz_len > horizontal_length_epsilon (> 0) guard.
    if (ops::ordered_zero(xz_len)) {
        const engine::Vec4 fallback = vector_zero.get();
        nx = fallback.x();
        ny = fallback.y();
        nz = fallback.z();
    } else if ((std::bit_cast<uint32_t>(xz_len) & float_magnitude_mask) == float_infinity_bits) {
        const engine::Vec4 fallback = vector_nan.get();
        nx = fallback.x();
        ny = fallback.y();
        nz = fallback.z();
    } else {
        nx = engine::divide(rx, xz_len);
        ny = engine::divide(0.0f, xz_len);
        nz = engine::divide(rz, xz_len);
    }

    // Matches the original's packed multiply (nx, ny, nz, 0.0f) * broadcast(xyz_len), where
    // lane w becomes 0.0f * xyz_len (producing NaN when xyz_len is infinite or NaN).
    return ops::multiply_in_order(ops::lanes(nx, ny, nz, 0.0f), ops::broadcast(xyz_len));
}

void run_step_phase(ChrData *self, float frame_time)
{
    engine::Vec4 motion_delta = engine::Vec4::zero();
    engine::Vec4 rotation_delta = engine::Vec4::zero();
    engine::Vec4 quantized_rotation = engine::Vec4::zero();
    engine::Vec4 orientation = self->owner->modules->state->default_orientation;

    DrawTarget *initial_target = active_target(self);
    const uint8_t visible = initial_target->vtable->visible(initial_target);

    uint8_t allow_a = 0;
    uint8_t allow_b = 0;
    if (visible || self->owner->modules->points->hp > 0) {
        motion_delta = self->motion_delta;
        rotation_delta = initial_target->rotation_delta;
        quantized_rotation = visible ? quantize_yaw(self, rotation_delta) : rotation_delta;
        orientation = initial_target->target_orientation;

        ChrIns *owner = self->owner;
        allow_a = (owner->modules->flags_module->flags_0x1bc & flags_block_motion_a) == 0 ? 1 : 0;
        // Original carries raw byte +0x1d8 and masks bit 0 before storing/passing.
        allow_b = self->motion_b_enabled & 1u;

        if (linked_blocks_motion(self, owner)) {
            allow_a = 0;
            allow_b = 0;
        }
        if (self->owner->modules->gate_module->active) {
            DrawTarget *gate_target = active_target(self);
            if (!gate_target->vtable->allow_motion_b(gate_target)) allow_b = 0;
        }
        if (self->motion_blocked) {
            allow_a = 0;
            allow_b = 0;
        } else {
            if (self->params->motion_mode != params_allow_motion_a) allow_a = 0;
            if ((self->mode_flags & mode_mask) != mode_active) {
                allow_a = 0;
                allow_b = 0;
            }
        }

        if (visible) {
            allow_a = 1;
            allow_b = 1;
        }

        const ChrModules *modules = self->owner->modules;
        const ThrowState *throw_state = modules->throw_request->state;
        if (!throw_state || throw_state->active) {
            allow_a = 0;
            allow_b = 0;
        }

        if (should_freeze_motion(self, modules)) {
            motion_delta = engine::Vec4::zero();
            rotation_delta = engine::Vec4::zero();
            const ChrIns *current_owner = self->owner;
            if (!(current_owner->flags_0x359 & chr_keep_orientation_on_freeze))
                orientation = current_owner->modules->state->default_orientation;
            allow_a = 0;
            allow_b = 0;
        }

        const engine::Vec4 extra = self->vtable->extra_rotation(self, allow_a != 0, frame_time);
        rotation_delta = ops::add_in_order(extra, rotation_delta);
    }

    DrawTarget *step_target = active_target(self);
    const uint8_t target_visible = step_target ? step_target->vtable->visible(step_target) : 0;
    self->motion_b_result = allow_b;
    self->motion_a_result = allow_a;
    self->vtable->apply_step(self, &motion_delta, &rotation_delta, &quantized_rotation, &orientation, frame_time,
                             allow_b, allow_a, target_visible);
}

}  // namespace

extern "C" void bb_character_data_update_01913c10(ChrData *self, float frame_time)
{
    if (anim_events_ready(self->owner)) run_step_phase(self, frame_time);

    if (DrawTarget *target = active_target(self)) {
        if ((self->mode_flags & mode_mask) == mode_active || target->vtable->visible(target))
            target->vtable->update_active(target, self, frame_time);
        else
            target->vtable->update_inactive(target);
    }

    ChrIns *owner = self->owner;
    if (!anim_events_ready(owner)) return;

    DrawTarget *motion_target = active_target(self);
    const engine::Vec4 local = motion_target->local_motion;
    const float rx = dot4_horizontal(local, self->basis_row_x);
    const float ry = dot4_horizontal(local, self->basis_row_y);
    const float rz = dot4_horizontal(local, self->basis_row_z);
    engine::Vec4 world_motion = ops::xyz_with_w(ops::lanes(rx, ry, rz, 0.0f), local);

    if (owner->vtable->project_horizontal_motion(owner)) world_motion = project_to_horizontal(rx, ry, rz);

    MotionReceiver *receiver = self->vtable->motion_receiver(self);
    receiver->vtable->apply_motion(receiver, &world_motion);
    self->motion_delta = world_motion;

    params_post_update(self->params, active_target(self), frame_time);
}
