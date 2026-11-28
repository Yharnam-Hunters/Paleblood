// SPDX-License-Identifier: GPL-2.0-or-later
// Character: layouts and originals for character_params_post_update (0x0190a5f0).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace character::params_post_update {

struct ChrData;
struct ChrIns;
struct ChrParams;
struct DrawTarget;
struct ThrowRequest;

struct AnimEvents {
    uint8_t unknown_0x000[0x384];
    uint8_t ready;
};
static_assert(offsetof(AnimEvents, ready) == 0x384);

struct EventGate {
    uint8_t unknown_0x00[0x18];
    uint8_t flags;
};
struct EventGateHolder {
    uint8_t unknown_0x00[0x30];
    EventGate *gate;
};
static_assert(offsetof(EventGate, flags) == 0x18 && offsetof(EventGateHolder, gate) == 0x30);
constexpr uint8_t gate_suppress = 0x01;

// Character flags module at modules +0x08:
// - flags_0x10 bit 0 selects ActionRequestModule::bits_0x20 over bits_0x30;
// - flags_0x10 bit 3 gates updating ChrIns bit 10 (flags_0x1e2 bit 2) from bit 4;
// - flags_0x818 bit 4 mirrors DrawTarget::flags_0x90 bit 4 on the visible path.
struct ChrFlagsModule {
    uint8_t unknown_0x000[0x10];
    uint8_t flags_0x10;
    uint8_t unknown_0x011[0x807];
    uint8_t flags_0x818;
};
static_assert(offsetof(ChrFlagsModule, flags_0x10) == 0x10 && offsetof(ChrFlagsModule, flags_0x818) == 0x818);
constexpr uint8_t flags_select_primary_bits = 0x01;
constexpr uint8_t flags_update_owner_bit10 = 0x08;
constexpr uint8_t flags_owner_bit10_value = 0x10;
constexpr uint8_t flags_0x818_sync_bit = 0x10;

struct ChrPointsData {
    uint8_t unknown_0x000[0x134];
    int32_t points;
};
static_assert(offsetof(ChrPointsData, points) == 0x134);

// Action request module at modules +0x80: supplies the action id copied to ChrParams +0xa0 and
// the primary/secondary 64-bit action masks checked before calling 0x01e60620.
struct ActionRequestModule {
    uint8_t unknown_0x00[0x8];
    ChrIns *owner;
    uint8_t unknown_0x10[0x10];
    uint64_t bits_0x20;
    uint8_t unknown_0x28[0x8];
    uint64_t bits_0x30;
    uint8_t unknown_0x38[0x5c];
    uint32_t action_id_0x94;
};
static_assert(offsetof(ActionRequestModule, owner) == 0x8 && offsetof(ActionRequestModule, bits_0x20) == 0x20);
static_assert(offsetof(ActionRequestModule, bits_0x30) == 0x30 &&
              offsetof(ActionRequestModule, action_id_0x94) == 0x94);
constexpr uint64_t action_bit_primary = 0x01;
constexpr uint64_t action_bit_secondary = 0x04;
constexpr uint32_t action_id_force_throw_check = 0x0c26;

struct ChrModules {
    uint8_t unknown_0x00[0x8];
    ChrFlagsModule *flags_module;
    uint8_t unknown_0x10[0x10];
    ChrPointsData *points;
    uint8_t unknown_0x28[0x8];
    AnimEvents *anim_events;
    uint8_t unknown_0x38[0x48];
    ActionRequestModule *action_request;
    ThrowRequest *throw_request;
};
static_assert(offsetof(ChrModules, flags_module) == 0x8 && offsetof(ChrModules, points) == 0x20);
static_assert(offsetof(ChrModules, anim_events) == 0x30 && offsetof(ChrModules, action_request) == 0x80);
static_assert(offsetof(ChrModules, throw_request) == 0x88);

struct ChrInsVtable {
    void *unknown_slots_0[136];
    void (*on_state_change)(ChrIns *, int32_t state);
};
static_assert(offsetof(ChrInsVtable, on_state_change) == 0x440);

// Character instance: bytes 0x1e1..0x1e5 hold a packed 40-bit flag field in the original
// (bit 10 = flags_0x1e2 bit 2; bit 27 = flags_0x1e4 bit 3).
struct ChrIns {
    const ChrInsVtable *vtable;
    uint8_t unknown_0x008[0x10];
    EventGateHolder *gate_holder;
    uint8_t unknown_0x020[0x38];
    ChrData *data;
    uint8_t unknown_0x060[0x164];
    int32_t state_0x1c4;
    uint8_t unknown_0x1c8[0x19];
    uint8_t flags_0x1e1;
    uint8_t flags_0x1e2;
    uint8_t flags_0x1e3;
    uint8_t flags_0x1e4;
    uint8_t flags_0x1e5;
    uint8_t unknown_0x1e6[0x1ca];
    ChrModules *modules;
};
static_assert(offsetof(ChrIns, gate_holder) == 0x18 && offsetof(ChrIns, data) == 0x58);
static_assert(offsetof(ChrIns, state_0x1c4) == 0x1c4 && offsetof(ChrIns, flags_0x1e1) == 0x1e1);
static_assert(offsetof(ChrIns, flags_0x1e2) == 0x1e2 && offsetof(ChrIns, flags_0x1e4) == 0x1e4);
static_assert(offsetof(ChrIns, flags_0x1e5) == 0x1e5 && offsetof(ChrIns, modules) == 0x3b0);
constexpr uint8_t chr_flags_0x1e2_bit = 0x04;
constexpr uint8_t chr_flags_0x1e4_bit = 0x08;

struct ChrData {
    uint8_t unknown_0x00[0x8];
    ChrIns *owner;
    uint8_t unknown_0x10[0x28];
    ChrParams *params;
};
static_assert(offsetof(ChrData, owner) == 0x8 && offsetof(ChrData, params) == 0x38);

struct ChrParams {
    uint8_t unknown_0x00[0x8];
    ChrData *data;
    uint8_t unknown_0x10[0x32];
    uint8_t flags_0x42;
    uint8_t unknown_0x43[0x19];
    uint16_t flags_0x5c;
    uint8_t unknown_0x5e[0x42];
    uint32_t action_id_0xa0;
};
static_assert(offsetof(ChrParams, data) == 0x8 && offsetof(ChrParams, flags_0x42) == 0x42);
static_assert(offsetof(ChrParams, flags_0x5c) == 0x5c && offsetof(ChrParams, action_id_0xa0) == 0xa0);
constexpr uint8_t params_suppress_owner_bit10 = 0x08;
constexpr uint16_t params_flags_0x5c_from_bit3 = 0x0040;
constexpr uint16_t params_flags_0x5c_from_bit0 = 0x0080;

// Virtual callbacks match the original's return-register test widths:
// - has_action_state (+0x90) is tested with 32-bit `test eax, eax`;
// - visible (+0xc8) and sync_flags_ready (+0xf8) are tested byte-wide with `test al, al`.
struct DrawTargetVtable {
    void *unknown_slots_0[18];
    uint32_t (*has_action_state)(DrawTarget *);
    void *unknown_slots_1[6];
    uint8_t (*visible)(DrawTarget *);
    void *unknown_slots_2[5];
    uint8_t (*sync_flags_ready)(DrawTarget *);
};
static_assert(offsetof(DrawTargetVtable, has_action_state) == 0x90);
static_assert(offsetof(DrawTargetVtable, visible) == 0xc8);
static_assert(offsetof(DrawTargetVtable, sync_flags_ready) == 0xf8);

struct DrawTarget {
    const DrawTargetVtable *vtable;
    uint8_t unknown_0x08[0x88];
    uint32_t flags_0x90;
};
static_assert(offsetof(DrawTarget, flags_0x90) == 0x90);
constexpr uint32_t target_flag_bit0 = 0x00000001u;
constexpr uint32_t target_flag_bit1 = 0x00000002u;
constexpr uint32_t target_flag_bit3 = 0x00000008u;
constexpr uint32_t target_flag_bit4 = 0x00000010u;
constexpr uint32_t target_state_shift = 24u;

constexpr uint32_t throw_check_mode_primary = 3u;
constexpr uint32_t throw_check_mode_secondary = 4u;

RT_ORIGINAL(0x01e60620, throw_request_check, void(ThrowRequest *, uint32_t mode, uint32_t flags));

}  // namespace character::params_post_update
