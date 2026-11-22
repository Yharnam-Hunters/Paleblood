// SPDX-License-Identifier: GPL-2.0-or-later
// Character: layouts and originals for character_data_update_01913c10 (0x01913c10).
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "runtime/original.h"

namespace character::data_update {

struct ChrData;
struct ChrIns;
struct DrawTarget;

struct AnimEvents {
    uint8_t unknown_0x000[0x384];
    uint8_t ready;
};
static_assert(offsetof(AnimEvents, ready) == 0x384);

struct ChrStateModule {
    uint8_t unknown_0x000[0x1d0];
    engine::Vec4 default_orientation;
};
static_assert(offsetof(ChrStateModule, default_orientation) == 0x1d0);

struct ChrPointsData {
    uint8_t unknown_0x000[0xf8];
    int32_t hp;
    uint8_t unknown_0x0fc[0x104];
    uint8_t flags;
};
static_assert(offsetof(ChrPointsData, hp) == 0xf8 && offsetof(ChrPointsData, flags) == 0x200);
constexpr uint8_t points_freeze_motion = 0x40;

// Flags module at modules +0x08: bit 0x8000 clears motion flag A when the target is not visible.
struct ChrFlagsModule {
    uint8_t unknown_0x000[0x1bc];
    uint32_t flags_0x1bc;
};
static_assert(offsetof(ChrFlagsModule, flags_0x1bc) == 0x1bc);
constexpr uint32_t flags_block_motion_a = 0x8000u;

struct ChrGateModule {
    uint8_t unknown_0x00[0x10];
    uint8_t active;
};
static_assert(offsetof(ChrGateModule, active) == 0x10);

struct ThrowState {
    uint8_t unknown_0x00[0x20];
    uint8_t active;
};
struct ThrowRequest {
    uint8_t unknown_0x00[0x28];
    ThrowState *state;
};
static_assert(offsetof(ThrowState, active) == 0x20 && offsetof(ThrowRequest, state) == 0x28);

struct ChrModules {
    uint8_t unknown_0x00[0x8];
    ChrFlagsModule *flags_module;
    uint8_t unknown_0x10[0x10];
    ChrPointsData *points;
    uint8_t unknown_0x28[0x8];
    AnimEvents *anim_events;
    uint8_t unknown_0x38[0x18];
    ChrGateModule *gate_module;
    uint8_t unknown_0x58[0x10];
    ChrStateModule *state;
    uint8_t unknown_0x70[0x18];
    ThrowRequest *throw_request;
};
static_assert(offsetof(ChrModules, flags_module) == 0x8 && offsetof(ChrModules, points) == 0x20);
static_assert(offsetof(ChrModules, anim_events) == 0x30 && offsetof(ChrModules, gate_module) == 0x50);
static_assert(offsetof(ChrModules, state) == 0x68 && offsetof(ChrModules, throw_request) == 0x88);

// Linked source reached through ChrIns::vtable +0x168 -> +0x08. Confirmed from disassembly:
// byte +0x130 is tested for 0 on the second linked() query and for 10 on the third; either
// clears both motion flags when the target is not visible (broader enum meaning is unconfirmed).
struct LinkedSource {
    uint8_t unknown_0x000[0x130];
    uint8_t state_0x130;
};
struct Linked {
    uint8_t unknown_0x00[0x8];
    LinkedSource *source;
};
static_assert(offsetof(LinkedSource, state_0x130) == 0x130 && offsetof(Linked, source) == 0x8);
constexpr uint8_t linked_state_blocked = 10;

struct ChrInsVtable {
    void *unknown_slots_0[45];
    Linked *(*linked)(ChrIns *);
    void *unknown_slots_1[8];
    bool (*debug_freeze_exempt_b)(ChrIns *);
    bool (*debug_freeze_exempt_a)(ChrIns *);
    void *unknown_slots_2[99];
    bool (*project_horizontal_motion)(ChrIns *);
};
static_assert(offsetof(ChrInsVtable, linked) == 0x168);
static_assert(offsetof(ChrInsVtable, debug_freeze_exempt_b) == 0x1b0);
static_assert(offsetof(ChrInsVtable, debug_freeze_exempt_a) == 0x1b8);
static_assert(offsetof(ChrInsVtable, project_horizontal_motion) == 0x4d8);

// Character instance: bit 0x20 of flags_0x359 keeps the target orientation when motion is frozen.
struct ChrIns {
    const ChrInsVtable *vtable;
    uint8_t unknown_0x008[0x351];
    uint8_t flags_0x359;
    uint8_t unknown_0x35a[0x56];
    ChrModules *modules;
};
static_assert(offsetof(ChrIns, flags_0x359) == 0x359 && offsetof(ChrIns, modules) == 0x3b0);
constexpr uint8_t chr_keep_orientation_on_freeze = 0x20;

struct ChrParams {
    uint8_t unknown_0x00[0x60];
    uint8_t motion_mode;
};
static_assert(offsetof(ChrParams, motion_mode) == 0x60);
constexpr uint8_t params_allow_motion_a = 0xff;

struct MotionReceiver;
struct MotionReceiverVtable {
    void *unknown_slots[6];
    void (*apply_motion)(MotionReceiver *, engine::Vec4 *);
};
static_assert(offsetof(MotionReceiverVtable, apply_motion) == 0x30);
struct MotionReceiver {
    const MotionReceiverVtable *vtable;
};

struct DrawTargetVtable {
    void *unknown_slots_0[8];
    void (*update_inactive)(DrawTarget *);
    void (*update_active)(DrawTarget *, ChrData *, float frame_time);
    void *unknown_slots_1[14];
    bool (*allow_motion_b)(DrawTarget *);
    uint8_t (*visible)(DrawTarget *);
};
static_assert(offsetof(DrawTargetVtable, update_inactive) == 0x40);
static_assert(offsetof(DrawTargetVtable, update_active) == 0x48);
static_assert(offsetof(DrawTargetVtable, allow_motion_b) == 0xc0);
static_assert(offsetof(DrawTargetVtable, visible) == 0xc8);

struct DrawTarget {
    const DrawTargetVtable *vtable;
    uint8_t unknown_0x08[0x8];
    engine::Vec4 local_motion;
    engine::Vec4 rotation_delta;
    engine::Vec4 target_orientation;
};
static_assert(offsetof(DrawTarget, local_motion) == 0x10);
static_assert(offsetof(DrawTarget, rotation_delta) == 0x20);
static_assert(offsetof(DrawTarget, target_orientation) == 0x30);

struct ChrDataVtable {
    void *unknown_slots_0[9];
    MotionReceiver *(*motion_receiver)(ChrData *);
    void *unknown_slots_1[6];
    engine::Vec4 (*extra_rotation)(ChrData *, bool allow_motion_a, float frame_time);
    void *unknown_slots_2[6];
    void (*apply_step)(ChrData *, const engine::Vec4 *motion_delta, const engine::Vec4 *rotation_delta,
                       const engine::Vec4 *quantized_rotation, const engine::Vec4 *orientation, float frame_time,
                       uint32_t allow_motion_b, uint32_t allow_motion_a, uint32_t target_visible);
    void *unknown_slots_3[4];
    int32_t (*turn_rate_scale)(ChrData *);
};
static_assert(offsetof(ChrDataVtable, motion_receiver) == 0x48);
static_assert(offsetof(ChrDataVtable, extra_rotation) == 0x80);
static_assert(offsetof(ChrDataVtable, apply_step) == 0xb8);
static_assert(offsetof(ChrDataVtable, turn_rate_scale) == 0xe0);

struct ChrData {
    const ChrDataVtable *vtable;
    ChrIns *owner;
    uint8_t unknown_0x010[0x28];
    ChrParams *params;
    uint8_t unknown_0x040[0x40];
    DrawTarget *fallback_target;
    uint8_t unknown_0x088[0x38];
    engine::Vec4 basis_row_x;
    engine::Vec4 basis_row_y;
    engine::Vec4 basis_row_z;
    uint8_t unknown_0x0f0[0x10];
    engine::Vec4 motion_delta;
    uint8_t mode_flags;        // low 3 bits == 7 enables active mode
    uint8_t motion_blocked;
    uint8_t unknown_0x112[0xc6];
    uint8_t motion_b_enabled;  // only bit 0 contributes to allow_b
    uint8_t motion_a_result;
    uint8_t unknown_0x1da[0xa6];
    DrawTarget *target;
    uint8_t unknown_0x288[0x2];
    uint8_t motion_b_result;
};
static_assert(offsetof(ChrData, owner) == 0x8 && offsetof(ChrData, params) == 0x38);
static_assert(offsetof(ChrData, fallback_target) == 0x80);
static_assert(offsetof(ChrData, basis_row_x) == 0xc0 && offsetof(ChrData, basis_row_y) == 0xd0);
static_assert(offsetof(ChrData, basis_row_z) == 0xe0 && offsetof(ChrData, motion_delta) == 0x100);
static_assert(offsetof(ChrData, mode_flags) == 0x110 && offsetof(ChrData, motion_blocked) == 0x111);
static_assert(offsetof(ChrData, motion_b_enabled) == 0x1d8 && offsetof(ChrData, motion_a_result) == 0x1d9);
static_assert(offsetof(ChrData, target) == 0x280 && offsetof(ChrData, motion_b_result) == 0x28a);

constexpr uint8_t mode_mask = 0x7;
constexpr uint8_t mode_active = 0x7;

RT_ORIGINAL(0x0190a5f0, params_post_update, void(ChrParams *, DrawTarget *, float frame_time));

RT_GLOBAL(0x04d262ec, rad_to_deg, const float);
RT_GLOBAL(0x04d262f0, yaw_threshold_high, const float);
RT_GLOBAL(0x04d262f4, yaw_threshold_mid, const float);
RT_GLOBAL(0x04d262f8, yaw_threshold_low, const float);
RT_GLOBAL(0x04d262fc, yaw_step_neg_quarter_pi, const float);
RT_GLOBAL(0x04d26300, yaw_step_pos_quarter_pi, const float);
RT_GLOBAL(0x04d26304, yaw_step_neg_half_pi, const float);
RT_GLOBAL(0x04d26308, yaw_step_pos_half_pi, const float);
RT_GLOBAL(0x04d2630c, yaw_step_neg_pi, const float);
RT_GLOBAL(0x04d26310, yaw_step_pos_pi, const float);
RT_GLOBAL(0x04d26314, horizontal_length_epsilon, const float);
RT_GLOBAL(0x04d35c4a, world_chr_man_dbg_name, const char);
RT_GLOBAL(0x0593e880, world_chr_man_dbg_instance, void *);
RT_GLOBAL(0x0593e894, debug_freeze_motion, const uint8_t);
RT_GLOBAL(0x05940540, vector_nan, const engine::Vec4);
RT_GLOBAL(0x05940550, vector_zero, const engine::Vec4);

}  // namespace character::data_update
