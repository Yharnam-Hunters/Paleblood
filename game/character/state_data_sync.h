// SPDX-License-Identifier: GPL-2.0-or-later
// Character: layouts and originals for character_state_data_sync (0x01cf7880).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace character::state_data_sync {

struct AssociatedObject;
struct ChrIns;
struct ChrStateData;
struct EffectList;
struct EffectNode;

struct EventGate {
    uint8_t unknown_0x00[0x18];
    uint8_t flags;
};
struct EventGateHolder {
    uint8_t unknown_0x00[0x30];
    EventGate *gate;
};
static_assert(offsetof(EventGate, flags) == 0x18 && offsetof(EventGateHolder, gate) == 0x30);
constexpr uint8_t event_gate_active_bit = 0x01;

// Points data module at modules +0x20: holds three signed value/limit pairs transferred into
// ChrStateData (+0x14/+0x18, +0x20/+0x24, +0x30/+0x34). +0xf8 is HP (data_update.h) and
// +0x134/+0x138 are points/max_points (frame_update.cpp); +0x128/+0x12c semantics are provisional.
struct ChrPointsData {
    uint8_t unknown_0x000[0xf8];
    int32_t value_0xf8;
    int32_t limit_0xfc;
    uint8_t unknown_0x100[0x28];
    int32_t value_0x128;
    int32_t limit_0x12c;
    uint8_t unknown_0x130[0x4];
    int32_t value_0x134;
    int32_t limit_0x138;
};
static_assert(offsetof(ChrPointsData, value_0xf8) == 0xf8 && offsetof(ChrPointsData, limit_0xfc) == 0xfc);
static_assert(offsetof(ChrPointsData, value_0x128) == 0x128 && offsetof(ChrPointsData, limit_0x12c) == 0x12c);
static_assert(offsetof(ChrPointsData, value_0x134) == 0x134 && offsetof(ChrPointsData, limit_0x138) == 0x138);

// Stepped counter module at modules +0x28 (same five counters refilled by 0x01e5c440):
// holds five current counters, five maxima, and a 32-bit flag word at +0x4c whose low 4 bits
// are mirrored into ChrStateData::flags_0x650 before the full word is cleared.
constexpr std::size_t counter_count = 5;
constexpr std::size_t counter_slot_0 = 0;
constexpr std::size_t counter_slot_1 = 1;
constexpr std::size_t counter_slot_2 = 2;
constexpr std::size_t counter_slot_3 = 3;
constexpr std::size_t counter_slot_4 = 4;

struct CounterModule {
    uint8_t unknown_0x00[0x10];
    int32_t counters[counter_count];
    int32_t maxima[counter_count];
    uint8_t unknown_0x38[0x14];
    uint32_t flags_0x4c;
};
static_assert(offsetof(CounterModule, counters) == 0x10 && offsetof(CounterModule, maxima) == 0x24);
static_assert(offsetof(CounterModule, flags_0x4c) == 0x4c);

struct ChrModules {
    uint8_t unknown_0x00[0x20];
    ChrPointsData *points;
    CounterModule *counters;
};
static_assert(offsetof(ChrModules, points) == 0x20 && offsetof(ChrModules, counters) == 0x28);

// Effect parameter entry referenced from EffectNode +0x48:
// - type_0x156 selects matching active nodes during the per-type scans;
// - bonus_0x13a is a signed 16-bit term accumulated onto vtable+0x2b8() for type 0x9b nodes.
struct EffectParam {
    uint8_t unknown_0x000[0x13a];
    int16_t bonus_0x13a;
    uint8_t unknown_0x13c[0x1a];
    uint16_t type_0x156;
};
static_assert(offsetof(EffectParam, bonus_0x13a) == 0x13a && offsetof(EffectParam, type_0x156) == 0x156);

struct EffectNode {
    float value_0x00;
    float value_0x04;
    uint8_t unknown_0x08[0x14];
    uint32_t flags;
    uint8_t unknown_0x20[0x28];
    EffectParam *param;
    uint8_t unknown_0x50[0x8];
    EffectNode *next;
};
static_assert(offsetof(EffectNode, value_0x00) == 0x00 && offsetof(EffectNode, value_0x04) == 0x04);
static_assert(offsetof(EffectNode, flags) == 0x1c && offsetof(EffectNode, param) == 0x48);
static_assert(offsetof(EffectNode, next) == 0x58);

struct EffectList {
    uint8_t unknown_0x00[0x8];
    EffectNode *head;
    uint8_t unknown_0x10[0x10];
    uint32_t active_mask;
};
static_assert(offsetof(EffectList, head) == 0x8 && offsetof(EffectList, active_mask) == 0x20);

constexpr uint32_t effect_inactive_mask = 0x800c0003u;
constexpr uint32_t mask_type_0x75 = 0x00000038u;
constexpr uint32_t mask_type_0x88 = 0x000001c0u;
constexpr uint32_t mask_type_2 = 0x00000e00u;
constexpr uint32_t mask_type_0x9b = 0x00000e00u;
constexpr uint32_t mask_type_5 = 0x001c0000u;
constexpr uint32_t mask_type_6 = 0x00e00000u;
constexpr uint32_t mask_type_0x74 = 0x38000000u;
constexpr uint32_t mask_type_0x104 = 0x38000000u;

constexpr uint16_t effect_type_2 = 0x0002u;
constexpr uint16_t effect_type_5 = 0x0005u;
constexpr uint16_t effect_type_6 = 0x0006u;
constexpr uint16_t effect_type_0x74 = 0x0074u;
constexpr uint16_t effect_type_0x75 = 0x0075u;
constexpr uint16_t effect_type_0x88 = 0x0088u;
constexpr uint16_t effect_type_0x9b = 0x009bu;
constexpr uint16_t effect_type_0x104 = 0x0104u;

constexpr float effect_absent_value = -1.0f;
constexpr float effect_special_value_a = 999.0f;
constexpr float effect_special_value_b = 1000.0f;
constexpr float associated_reset_value = -1.0f;

struct AssociatedObject {
    uint8_t unknown_0x00[0x28];
    float reset_0x28;
};
static_assert(offsetof(AssociatedObject, reset_0x28) == 0x28);

// Character state object returned by ChrIns::vtable +0x1c8 (self +0x3c0 on vtable 0x0578f310).
constexpr std::size_t effect_float_count = 4;
constexpr std::size_t effect_slot_0 = 0;
constexpr std::size_t effect_slot_1 = 1;
constexpr std::size_t effect_slot_2 = 2;
constexpr std::size_t effect_slot_3 = 3;
constexpr int32_t state_cap_sentinel = 999999;  // 0x000f423f
constexpr uint8_t state_flag_counter_mask = 0x0f;
constexpr uint8_t state_flag_gate_bit = 0x10;

struct ChrStateData {
    uint8_t unknown_0x000[0x14];
    int32_t value_0x14;
    int32_t limit_0x18;
    uint8_t unknown_0x01c[0x4];
    int32_t value_0x20;
    int32_t limit_0x24;
    uint8_t unknown_0x028[0x8];
    int32_t value_0x30;
    int32_t limit_0x34;
    uint8_t unknown_0x038[0xc8];
    int32_t raw_counters[counter_count];
    uint8_t unknown_0x114[0x4e0];
    int32_t cap_0x5f4;
    uint8_t unknown_0x5f8[0xc];
    int32_t cap_0x604;
    uint8_t unknown_0x608[0x14];
    int32_t active_counters[counter_count];
    float effect_values_a[effect_float_count];
    float effect_values_b[effect_float_count];
    uint8_t flags_0x650;
    uint8_t unknown_0x651[0x3];
    int32_t counter_maxima[counter_count];
    int32_t stat_0x668;
    int32_t effect_mode_0x66c;
    AssociatedObject *assoc_first;
    AssociatedObject *assoc_second;
};
static_assert(offsetof(ChrStateData, value_0x14) == 0x14 && offsetof(ChrStateData, limit_0x18) == 0x18);
static_assert(offsetof(ChrStateData, value_0x20) == 0x20 && offsetof(ChrStateData, limit_0x24) == 0x24);
static_assert(offsetof(ChrStateData, value_0x30) == 0x30 && offsetof(ChrStateData, limit_0x34) == 0x34);
static_assert(offsetof(ChrStateData, raw_counters) == 0x100 && offsetof(ChrStateData, cap_0x5f4) == 0x5f4);
static_assert(offsetof(ChrStateData, cap_0x604) == 0x604 && offsetof(ChrStateData, active_counters) == 0x61c);
static_assert(offsetof(ChrStateData, effect_values_a) == 0x630 && offsetof(ChrStateData, effect_values_b) == 0x640);
static_assert(offsetof(ChrStateData, flags_0x650) == 0x650 && offsetof(ChrStateData, counter_maxima) == 0x654);
static_assert(offsetof(ChrStateData, stat_0x668) == 0x668 && offsetof(ChrStateData, effect_mode_0x66c) == 0x66c);
static_assert(offsetof(ChrStateData, assoc_first) == 0x670 && offsetof(ChrStateData, assoc_second) == 0x678);

// Virtual callbacks match the original's return-register ABI:
// - skip_full_state_sync (+0x1c0) is tested byte-wide with `test al, al`;
// - state_data (+0x1c8) returns a ChrStateData* pointer (null causes immediate return);
// - base_stat_0x2b8 (+0x2b8) returns a single-precision float in xmm0.
struct ChrInsVtable {
    void *unknown_slots_0[56];
    uint8_t (*skip_full_state_sync)(ChrIns *);
    ChrStateData *(*state_data)(ChrIns *);
    void *unknown_slots_1[29];
    float (*base_stat_0x2b8)(ChrIns *);
};
static_assert(offsetof(ChrInsVtable, skip_full_state_sync) == 0x1c0);
static_assert(offsetof(ChrInsVtable, state_data) == 0x1c8);
static_assert(offsetof(ChrInsVtable, base_stat_0x2b8) == 0x2b8);

struct ChrIns {
    const ChrInsVtable *vtable;
    uint8_t unknown_0x008[0x10];
    EventGateHolder *gate_holder;
    uint8_t unknown_0x020[0x1a8];
    EffectList *effects;
    uint8_t unknown_0x1d0[0x1e0];
    ChrModules *modules;
};
static_assert(offsetof(ChrIns, gate_holder) == 0x18 && offsetof(ChrIns, effects) == 0x1c8);
static_assert(offsetof(ChrIns, modules) == 0x3b0);

RT_ORIGINAL(0x01cfcaf0, state_sync_step_01cfcaf0, void(ChrIns *));
RT_ORIGINAL(0x01cfccb0, state_sync_step_01cfccb0, void(ChrIns *));
RT_ORIGINAL(0x01cfd110, state_sync_step_01cfd110, void(ChrIns *));
RT_ORIGINAL(0x01979450, query_effect_mode_01979450, int32_t(EffectList *));
RT_ORIGINAL(0x01be1860, sync_associated_effects_01be1860, void(AssociatedObject *, EffectList *));
RT_ORIGINAL(0x01bec080, process_effect_node_01bec080, void(EffectNode *, ChrStateData *));

}  // namespace character::state_data_sync
