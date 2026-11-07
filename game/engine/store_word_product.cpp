// SPDX-License-Identifier: GPL-2.0-or-later
// Store one word and a scaled product in the engine owner's two unknown DWORD fields.
// Called by 0x01111bb0 and through slot 1 of the table at 0x0500ae48; field meanings
// remain unknown. Packed fields preserve valid unaligned owner views.
#include <cstddef>
#include <cstdint>

namespace {
#pragma pack(push, 1)
struct WordProductFields {
    uint8_t unknown_0x00[0x18];
    volatile uint32_t input_word;
    volatile uint32_t scaled_product;
};
#pragma pack(pop)
static_assert(offsetof(WordProductFields, input_word) == 0x18);
static_assert(offsetof(WordProductFields, scaled_product) == 0x1c);
static_assert(sizeof(WordProductFields) == 0x20);
static_assert(alignof(WordProductFields) == 1);
constexpr uint32_t product_scale = 4;
}

extern "C" uint64_t bb_engine_store_word_product(WordProductFields *owner, uint32_t word_input,
                                               uint32_t factor0, uint32_t factor1)
{
    // Preserve the original pair of four-byte stores in order.
    owner->input_word = word_input;
    owner->scaled_product = factor0 * factor1 * product_scale;
    return 0;
}
