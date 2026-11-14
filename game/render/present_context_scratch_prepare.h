// SPDX-License-Identifier: GPL-2.0-or-later
// Two nine-slot banks and the record prefix inspected before context finishing.
// Resource preparation submits changed entries from the selected banks.
#pragma once

#include "byte_field.h"

#include "runtime/original.h"

namespace render {
struct PresentGpuContext;

struct ContextScratchResource {
    uint8_t unknown_0x00[0x18];
    static constexpr auto payload_18_extent_0 = 0x23;
    uint8_t payload_18[payload_18_extent_0];
    Field<uint8_t> flags_3b;
};
static_assert(offsetof(ContextScratchResource, payload_18) == 0x18);
static_assert(offsetof(ContextScratchResource, flags_3b) == 0x3b);

struct ContextScratchParameters {
    Field<uint32_t> first;
    Field<uint32_t> second;
};
static_assert(sizeof(ContextScratchParameters) == 0x8);
struct ContextScratchRecord {
    uint8_t unknown_0x00[0x8];
    Field<ContextScratchResource *> resource;
    static constexpr auto descriptor_10_extent_0 = 0x30;
    uint8_t descriptor_10[descriptor_10_extent_0];
    Field<uint32_t> type_40;
    uint8_t unknown_0x44[0x4];
    ContextScratchParameters parameters_48;
    uint8_t unknown_0x50[0x4];
    Field<uint32_t> parameter_54;
    Field<uint32_t> parameter_58;
    Field<uint32_t> word_5c;
};
static_assert(offsetof(ContextScratchRecord, resource) == 0x8);
static_assert(offsetof(ContextScratchRecord, descriptor_10) == 0x10);
static_assert(offsetof(ContextScratchRecord, type_40) == 0x40);
static_assert(offsetof(ContextScratchRecord, parameters_48) == 0x48);
static_assert(offsetof(ContextScratchRecord, parameter_54) == 0x54);
static_assert(offsetof(ContextScratchRecord, parameter_58) == 0x58);
static_assert(offsetof(ContextScratchRecord, word_5c) == 0x5c);

struct ContextScratchBank {
    static constexpr auto slots_extent_0 = 8;
    Field<uintptr_t> slots[slots_extent_0];
    Field<uintptr_t> last;
};
static_assert(offsetof(ContextScratchBank, last) == 0x40);
static_assert(sizeof(ContextScratchBank) == 0x48);
struct ContextScratchPrepareState {
    uint8_t unknown_0x00[0x60];
    static constexpr auto banks_extent_0 = 2;
    ContextScratchBank banks[banks_extent_0];
    uint8_t unknown_0xf0[0x98];
    Field<uint32_t> bank_flags;
};
static_assert(offsetof(ContextScratchPrepareState, banks) == 0x60);
static_assert(offsetof(ContextScratchPrepareState, bank_flags) == 0x188);
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
struct ContextScratchSnapshot {
    static constexpr auto slots_extent_0 = 9;
    Field<uintptr_t> slots[slots_extent_0];
};
static_assert(sizeof(ContextScratchSnapshot) == 0x48);
namespace context_scratch_calls {
// Refresh reads the preparation state and context before each ordinary submission.
RT_ORIGINAL(0x02ad1f80, refresh_state,
            void(ContextScratchPrepareState *, PresentGpuContext *));
RT_ORIGINAL(0x02ab5a70, present_record_consume,
            void(PresentGpuContext *, void *descriptor, void *resource_data,
                      uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02ab6810, submit_last,
            void(PresentGpuContext *, void *descriptor, void *resource_data,
                      uint32_t, uint32_t, uint32_t));
}  // namespace context_scratch_calls
}  // namespace render
