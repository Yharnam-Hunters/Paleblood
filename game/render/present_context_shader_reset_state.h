// SPDX-License-Identifier: GPL-2.0-or-later
// Shader state regions reset before presentation commands are submitted.
// The separate fill spans leave the intervening padding untouched.
#pragma once

#include "byte_field.h"


namespace render {
struct ContextShaderResetGroup {
    static constexpr auto words_extent_0 = 5;
    Field<uint64_t> words[words_extent_0];
    static constexpr auto payload_extent_0 = 0x610;
    uint8_t payload[payload_extent_0];
    uint8_t unknown_0x638[0x8];
};
static_assert(offsetof(ContextShaderResetGroup, payload) == 0x28);
static_assert(sizeof(ContextShaderResetGroup) == 0x640);
struct ContextShaderResetState {
    Field<uint32_t> word_00;
    Field<uint32_t> word_04;
    Field<uint32_t> word_08;
    uint8_t unknown_0x0c[0x4];
    Field<uint64_t> word_10;
    static constexpr auto groups_extent_0 = 6;
    ContextShaderResetGroup groups[groups_extent_0];
    uint8_t unknown_0x2598[0x8];
    Field<uint32_t> word_25a0;
    uint8_t unknown_0x25a4[0x4];
    Field<uint64_t> word_25a8;
    static constexpr auto block_25b0_extent_0 = 0x108;
    uint8_t block_25b0[block_25b0_extent_0];
    uint8_t unknown_0x26b8[0x8];
    static constexpr auto block_26c0_extent_0 = 0x20e;
    uint8_t block_26c0[block_26c0_extent_0];
    uint8_t unknown_0x28ce[0x2];
    Field<uint64_t> word_28d0;
    Field<uint64_t> word_28d8;
    uint8_t unknown_0x28e0[0x7bc0];
    static constexpr auto block_a4a0_extent_0 = 0xe00;
    uint8_t block_a4a0[block_a4a0_extent_0];
};
static_assert(offsetof(ContextShaderResetState, word_04) == 0x4);
static_assert(offsetof(ContextShaderResetState, word_08) == 0x8);
static_assert(offsetof(ContextShaderResetState, word_10) == 0x10);
static_assert(offsetof(ContextShaderResetState, groups) == 0x18);
static_assert(offsetof(ContextShaderResetState, word_25a0) == 0x25a0);
static_assert(offsetof(ContextShaderResetState, word_25a8) == 0x25a8);
static_assert(offsetof(ContextShaderResetState, block_25b0) == 0x25b0);
static_assert(offsetof(ContextShaderResetState, block_26c0) == 0x26c0);
static_assert(offsetof(ContextShaderResetState, word_28d0) == 0x28d0);
static_assert(offsetof(ContextShaderResetState, word_28d8) == 0x28d8);
static_assert(offsetof(ContextShaderResetState, block_a4a0) == 0xa4a0);
static_assert(sizeof(ContextShaderResetState) == 0xb2a0);
}  // namespace render
