// SPDX-License-Identifier: GPL-2.0-or-later
// Restore color and depth targets from independently selected banks. The color-target
// slot uses unsigned address arithmetic rather than imposing a bounds check.
#pragma once

#include <cstring>

#include "byte_field.h"

#include "present_preparation_packets.h"

namespace render {
struct TargetRestoreBank {
    static constexpr auto color_target_count = 8;
    Field<uintptr_t> color_targets[color_target_count];
    Field<uintptr_t> depth_target;
};
static_assert(offsetof(TargetRestoreBank, color_targets) == 0x00);
static_assert(offsetof(TargetRestoreBank, depth_target) == 0x40);
static_assert(sizeof(TargetRestoreBank) == 0x48);

struct TargetRestoreState {
    uint8_t unknown_0x0000[0x60];
    static constexpr auto bank_count = 2;
    TargetRestoreBank banks[bank_count];
    uint8_t unknown_0x00f0[0x98];
    Field<uint32_t> bank_flags;
};
static_assert(offsetof(TargetRestoreState, banks) == 0x60);
static_assert(offsetof(TargetRestoreState, bank_flags) == 0x188);
static_assert(sizeof(TargetRestoreState) == 0x18c);

constexpr uint32_t color_target_bank_bit = 0;
constexpr uint32_t depth_target_bank_bit = 1;
constexpr uintptr_t target_descriptor_prefix = 16;

inline const void *target_descriptor_payload(uintptr_t candidate)
{
    // Zero and all-ones candidates mean no target. Other candidates advance past the
    // descriptor prefix using wrapping unsigned arithmetic.
    return candidate == 0 || candidate == UINTPTR_MAX ? nullptr :
        reinterpret_cast<const void *>(candidate + target_descriptor_prefix);
}

inline uintptr_t color_target_source(TargetRestoreState *state, uint32_t bank, uint32_t slot)
{
    const uintptr_t origin = reinterpret_cast<uintptr_t>(state) +
        offsetof(TargetRestoreState, banks) + uint64_t(bank) * sizeof(TargetRestoreBank) +
        uint64_t(slot) * sizeof(Field<uintptr_t>);
    uintptr_t value;
    std::memcpy(&value, reinterpret_cast<const void *>(origin), sizeof(value));
    return value;
}
}  // namespace render
