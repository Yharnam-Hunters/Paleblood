// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the render record's bitfield setters and readers, and the
// small predicates and encodings next to them. They share one file on purpose: clang 3.4 lays out
// render_record_auxiliary_predicate's switch like the original only with the setters before it in
// the same unit (its block placement depends on what else the unit contains).
#include <stdbool.h>
#include <stdint.h>
#include "record.h"

#define WORD_08_LOW22_KEEP 0xffc00000u
#define WORD_08_LOW22_MASK 0x003fffffu

#define WORD_14_SECOND_LOW5_KEEP 0xfffffc1fu
#define LOW5_MASK 0x1fu
#define WORD_14_SECOND_LOW5_SHIFT 5

#define WORD_10_FIRST_AUXILIARY_KEEP 0xffffdfffu
#define WORD_10_FIRST_AUXILIARY_SHIFT 13

#define PAIR_KEEP 0xff001800u
#define PAIR_LOW_MASK 0x7ffu
#define PAIR_HIGH_SHIFT 13
#define PAIR_HIGH_MASK 0x00ffe000u

#define WORD_04_REPEATED_KEEP 0x800ff800u
#define WORD_04_REPEATED_LOW_MASK 0x7ffu
#define WORD_04_REPEATED_HIGH_SHIFT 20
#define WORD_04_REPEATED_HIGH_MASK 0x7ff00000u

#define DESCRIPTOR_INVALID_FROM 0xe0000000u
#define DESCRIPTOR_LEVELS_SHIFT 16
#define DESCRIPTOR_LEVELS_MASK 0xfu

// Bits 0..21 of word 0x08.
uint32_t render_record_set_word_08_low22(RenderRecord *record, uint32_t value)
{
    uint32_t kept = record->word_08 & WORD_08_LOW22_KEEP;
    record->word_08 = kept | (value & WORD_08_LOW22_MASK);
    return kept;
}

// Bits 5..9 of word 0x14 (its second 5-bit field).
uint32_t render_record_set_word_14_second_low5_field(RenderRecord *record, uint32_t value)
{
    uint32_t kept = record->word_14 & WORD_14_SECOND_LOW5_KEEP;
    record->word_14 = kept | ((value & LOW5_MASK) << WORD_14_SECOND_LOW5_SHIFT);
    return kept;
}

// Bit 13 of word 0x10. Matches only with a `bool` parameter: the original takes a C++ bool.
uint32_t render_record_set_first_auxiliary_flag(RenderRecord *record, bool enabled)
{
    uint32_t kept = record->word_10 & WORD_10_FIRST_AUXILIARY_KEEP;
    record->word_10 = kept | ((uint32_t)enabled << WORD_10_FIRST_AUXILIARY_SHIFT);
    return kept;
}

// Two 11-bit fields of word 0x0c: bits 0..10 and 13..23.
uint32_t render_record_set_word_0c_fields(RenderRecord *record, uint32_t low, uint32_t high)
{
    uint32_t kept = record->word_0c & PAIR_KEEP;
    record->word_0c = kept | (low & PAIR_LOW_MASK) | ((high << PAIR_HIGH_SHIFT) & PAIR_HIGH_MASK);
    return kept;
}

// The same 11-bit value in bits 0..10 and 20..30 of word 0x04.
uint32_t render_record_set_word_04_repeated_field(RenderRecord *record, uint32_t value)
{
    uint32_t kept = record->word_04 & WORD_04_REPEATED_KEEP;
    record->word_04 = kept | (value & WORD_04_REPEATED_LOW_MASK)
                    | ((value << WORD_04_REPEATED_HIGH_SHIFT) & WORD_04_REPEATED_HIGH_MASK);
    return kept;
}

// The level count in bits 16..19 of word 0x0c; 0 when the top three bits are all set.
uint32_t render_record_descriptor_level_count(const RenderRecord *record)
{
    uint32_t word = record->word_0c;
    if (word >= DESCRIPTOR_INVALID_FROM)
        return 0;
    return (word >> DESCRIPTOR_LEVELS_SHIFT) & DESCRIPTOR_LEVELS_MASK;
}

enum {
    AUXILIARY_ALWAYS_FIRST = 0x13,
    AUXILIARY_ALWAYS_SECOND = 0x14,
    AUXILIARY_FLAGGED_FIRST = 0x15,
    AUXILIARY_FLAGGED_SECOND = 0x16,
    AUXILIARY_FLAG_SHIFT = 6,
    AUXILIARY_FLAG = 1 << AUXILIARY_FLAG_SHIFT,
};

enum {
    ENCODING_FOUR = 4,
    ENCODING_TWO = 2,
    ENCODING_FOUR_FLAGS = 3,
};

// Selectors 0x13 and 0x14 always have the auxiliary form; 0x15 and 0x16 only when bit 6 of the
// flags is set; every other selector never.
bool render_record_auxiliary_predicate(uint32_t flags, uint32_t selector)
{
    switch (selector) {
    case AUXILIARY_ALWAYS_FIRST:
    case AUXILIARY_ALWAYS_SECOND:
        return true;
    case AUXILIARY_FLAGGED_FIRST:
    case AUXILIARY_FLAGGED_SECOND:
        return (flags & AUXILIARY_FLAG) >> AUXILIARY_FLAG_SHIFT;
    default:
        return false;
    }
}

// The flag bits for an encoding byte: 4 gives 3, 2 gives 1, anything else 0.
uint32_t render_record_encoding_byte_flags(const uint8_t *encoding)
{
    switch (*encoding) {
    case ENCODING_FOUR:
        return ENCODING_FOUR_FLAGS;
    case ENCODING_TWO:
        return 1;
    default:
        return 0;
    }
}

// A tuple parameter is supported when it is a power of two.
bool render_record_tuple_parameter_supported(uint32_t parameter)
{
    return parameter != 0 && (parameter & (parameter - 1)) == 0;
}
