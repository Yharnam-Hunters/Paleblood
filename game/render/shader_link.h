// SPDX-License-Identifier: GPL-2.0-or-later
// Pair-key matching and SPI_PS_INPUT_CNTL packets used by shader linking.
#pragma once
#include "command_reservation.h"
#include <cstddef>
#include <cstring>
namespace render {
constexpr uint32_t shader_link_word_capacity = 32;
struct ShaderLinkInputPair {
    uint8_t key;
    uint8_t value;
    uint16_t word() const
    {
        uint16_t result;
        std::memcpy(&result, this, sizeof(result));
        return result;
    }
};
static_assert(sizeof(ShaderLinkInputPair) == 2);
static_assert(offsetof(ShaderLinkInputPair, value) == 1);
struct ShaderLinkCommandHeader {
    Field<uint32_t> header;
    Field<uint32_t> register_index;
};
static_assert(sizeof(ShaderLinkCommandHeader) == 8);
static_assert(offsetof(ShaderLinkCommandHeader, register_index) == 4);

}  // namespace render
