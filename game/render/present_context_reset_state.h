// SPDX-License-Identifier: GPL-2.0-or-later
// Byte-backed reset views for three embedded presentation-context states. Padding
// remains untouched; scalar copies preserve live bits, including NaN payloads.
#pragma once

#include "byte_field.h"


namespace render {
struct ContextPairResetState {
    Field<uint64_t> word_00;
    Field<uint64_t> word_08;
};
static_assert(offsetof(ContextPairResetState, word_00) == 0x00);
static_assert(offsetof(ContextPairResetState, word_08) == 0x08);
static_assert(sizeof(ContextPairResetState) == 0x10);

struct ContextScalarResetState {
    Field<uint32_t> invalid_00;
    Field<uint32_t> invalid_04;
    Field<uint32_t> scalar_08;
    Field<uint32_t> scalar_0c;
    Field<uint32_t> scalar_10;
    Field<uint32_t> state_14;
    Field<uint32_t> invalid_18;
    uint8_t unknown_0x1c[0x04];
    Field<uint64_t> invalid_20;
    Field<uint64_t> packed_28;
};
static_assert(offsetof(ContextScalarResetState, invalid_00) == 0x00);
static_assert(offsetof(ContextScalarResetState, invalid_04) == 0x04);
static_assert(offsetof(ContextScalarResetState, scalar_08) == 0x08);
static_assert(offsetof(ContextScalarResetState, scalar_0c) == 0x0c);
static_assert(offsetof(ContextScalarResetState, scalar_10) == 0x10);
static_assert(offsetof(ContextScalarResetState, state_14) == 0x14);
static_assert(offsetof(ContextScalarResetState, invalid_18) == 0x18);
static_assert(offsetof(ContextScalarResetState, invalid_20) == 0x20);
static_assert(offsetof(ContextScalarResetState, packed_28) == 0x28);
static_assert(sizeof(ContextScalarResetState) == 0x30);

struct ContextPreparationReset {
    Field<uint32_t> invalid_00;
    uint8_t unknown_0x04[0x04];
    Field<uint64_t> invalid_08;
    static constexpr auto slot_indices_extent_0 = 8;
    Field<uint32_t> slot_indices[slot_indices_extent_0];
    static constexpr auto scalar_words_extent_0 = 4;
    Field<uint32_t> scalar_words[scalar_words_extent_0];
    Field<uint64_t> state_40;
    Field<uint32_t> invalid_48;
    Field<uint32_t> invalid_4c;
    Field<uint32_t> state_50;
    uint8_t unknown_0x54[0x04];
    Field<uint64_t> invalid_58;
    static constexpr auto banks_extent_0 = 2;
    static constexpr auto banks_extent_1 = 9;
    Field<uint64_t> banks[banks_extent_0][banks_extent_1];
    Field<uint32_t> invalid_f0;
    Field<uint32_t> invalid_f4;
    uint8_t unknown_0xf8[0x06];
    Field<uint8_t> state_fe;
    Field<uint8_t> state_ff;
    uint8_t unknown_0x100[0x40];
    Field<uint32_t> scalar_140;
    Field<uint16_t> state_144;
    Field<uint16_t> packed_146;
    static constexpr auto slot_states_extent_0 = 8;
    Field<uint64_t> slot_states[slot_states_extent_0];
    Field<uint32_t> bank_flags;
    uint8_t unknown_0x18c[0x04];
    Field<uint64_t> invalid_190;
    Field<uint64_t> invalid_198;
};
static_assert(offsetof(ContextPreparationReset, invalid_00) == 0x00);
static_assert(offsetof(ContextPreparationReset, invalid_08) == 0x08);
static_assert(offsetof(ContextPreparationReset, slot_indices) == 0x10);
static_assert(offsetof(ContextPreparationReset, scalar_words) == 0x30);
static_assert(offsetof(ContextPreparationReset, state_40) == 0x40);
static_assert(offsetof(ContextPreparationReset, invalid_48) == 0x48);
static_assert(offsetof(ContextPreparationReset, invalid_4c) == 0x4c);
static_assert(offsetof(ContextPreparationReset, state_50) == 0x50);
static_assert(offsetof(ContextPreparationReset, invalid_58) == 0x58);
static_assert(offsetof(ContextPreparationReset, banks) == 0x60);
static_assert(sizeof(ContextPreparationReset::banks[0]) == 0x48);
static_assert(offsetof(ContextPreparationReset, invalid_f0) == 0xf0);
static_assert(offsetof(ContextPreparationReset, invalid_f4) == 0xf4);
static_assert(offsetof(ContextPreparationReset, state_fe) == 0xfe);
static_assert(offsetof(ContextPreparationReset, state_ff) == 0xff);
static_assert(offsetof(ContextPreparationReset, scalar_140) == 0x140);
static_assert(offsetof(ContextPreparationReset, state_144) == 0x144);
static_assert(offsetof(ContextPreparationReset, packed_146) == 0x146);
static_assert(offsetof(ContextPreparationReset, slot_states) == 0x148);
static_assert(offsetof(ContextPreparationReset, bank_flags) == 0x188);
static_assert(offsetof(ContextPreparationReset, invalid_190) == 0x190);
static_assert(offsetof(ContextPreparationReset, invalid_198) == 0x198);
static_assert(sizeof(ContextPreparationReset) == 0x1a0);

}  // namespace render
