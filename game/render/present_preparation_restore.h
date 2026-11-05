// SPDX-License-Identifier: GPL-2.0-or-later
// Restore sources are selected independently by flag bits 0 and 1. The slot helper's
// index is an unbounded uint32; its native address arithmetic is retained explicitly.
#pragma once

#include "byte_field.h"

#include "secondary_calls.h"
#include "present_preparation_packets.h"

namespace render {
static_assert(sizeof(uintptr_t) == 8);

struct PreparationRestoreBank {
    static constexpr auto records_extent_0 = 8;
    Field<uintptr_t> records[records_extent_0];
    Field<uintptr_t> selector;
};
static_assert(offsetof(PreparationRestoreBank, records) == 0x00);
static_assert(offsetof(PreparationRestoreBank, selector) == 0x40);
static_assert(sizeof(PreparationRestoreBank) == 0x48);

struct PreparationRestoreState {
    uint8_t unknown_0x0000[0x60];
    static constexpr auto banks_extent_0 = 2;
    PreparationRestoreBank banks[banks_extent_0];
    uint8_t unknown_0x00f0[0x98];
    Field<uint32_t> bank_flags;
};
static_assert(offsetof(PreparationRestoreState, banks) == 0x60);
static_assert(offsetof(PreparationRestoreState, bank_flags) == 0x188);
static_assert(sizeof(PreparationRestoreState) == 0x18c);

constexpr uint32_t preparation_record_bank_bit = 0;
constexpr uint32_t preparation_selector_bank_bit = 1;
constexpr uintptr_t preparation_payload_prefix = 16;

inline const void *preparation_payload(uintptr_t candidate)
{
    // This is the native unsigned(candidate+1)>1 admission, including wraparound
    // in the accepted candidate+16 calculation. Other high-bit values remain admitted.
    return candidate == 0 || candidate == UINTPTR_MAX ? nullptr :
        reinterpret_cast<const void *>(candidate + preparation_payload_prefix);
}

inline uintptr_t preparation_record_source(PreparationRestoreState *state,
                                           uint32_t bank, uint32_t slot)
{
    const uintptr_t origin = reinterpret_cast<uintptr_t>(state) +
        offsetof(PreparationRestoreState, banks) + uint64_t(bank) * sizeof(PreparationRestoreBank) +
        uint64_t(slot) * sizeof(Field<uintptr_t>);
    uintptr_t value;
    std::memcpy(&value, reinterpret_cast<const void *>(origin), sizeof(value));
    return value;
}
}  // namespace render
