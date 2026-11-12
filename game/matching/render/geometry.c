// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the render record geometry flag initializer and the
// small geometry and tuple classification helpers next to it.
#include <stdbool.h>
#include <stdint.h>

#define GEOMETRY_UNSUPPORTED 0x80d30000u

enum {
    PARAMETER_FLAG_SHIFT = 12,
    FLAGS_SELECTOR_4 = 2,
    FLAGS_SELECTOR_5 = 4,
    FLAGS_SELECTOR_6 = 0x40,
    FLAGS_WIDE = 0x20,
    FLAGS_NARROW = 0x10,
};

enum {
    SELECTOR_CLEAR_FIRST = 1,
    SELECTOR_CLEAR_SECOND = 2,
    SELECTOR_UNSUPPORTED = 3,
    SELECTOR_4 = 4,
    SELECTOR_5 = 5,
    SELECTOR_6 = 6,
    SELECTOR_PLAIN_FIRST = 7,
    SELECTOR_WIDE_FIRST = 8,
    SELECTOR_NARROW_FIRST = 9,
    SELECTOR_PLAIN_SECOND = 10,
    SELECTOR_WIDE_SECOND = 11,
    SELECTOR_NARROW_SECOND = 12,
};

// Clear *output, then store the flags for a selector; selectors 7..12 add the parameter flag
// when the parameter is one. Unknown selectors (and a NULL output) are unsupported.
uint32_t render_initialize_record_geometry_flags(uint32_t *output, uint32_t selector,
                                                 uint32_t parameter)
{
    if (!output)
        return GEOMETRY_UNSUPPORTED;
    *output = 0;
    switch (selector) {
    case SELECTOR_CLEAR_FIRST:
    case SELECTOR_CLEAR_SECOND:
        break;
    case SELECTOR_4:
        *output = FLAGS_SELECTOR_4;
        break;
    case SELECTOR_5:
        *output = FLAGS_SELECTOR_5;
        break;
    case SELECTOR_6:
        *output = FLAGS_SELECTOR_6;
        break;
    case SELECTOR_PLAIN_FIRST:
    case SELECTOR_PLAIN_SECOND:
        *output = (parameter == 1) << PARAMETER_FLAG_SHIFT;
        break;
    case SELECTOR_WIDE_FIRST:
    case SELECTOR_WIDE_SECOND:
        *output = ((parameter == 1) << PARAMETER_FLAG_SHIFT) | FLAGS_WIDE;
        break;
    case SELECTOR_NARROW_FIRST:
    case SELECTOR_NARROW_SECOND:
        *output = ((parameter == 1) << PARAMETER_FLAG_SHIFT) | FLAGS_NARROW;
        break;
    default:
        return GEOMETRY_UNSUPPORTED;
    }
    return 0;
}

#define WORD_BITS 32

// The smallest power of two at or above value; 0 and values above 2^31 give 0.
uint32_t render_record_geometry_round_up_power_of_two(uint32_t value)
{
    uint32_t shift;
    value--;
    for (shift = 1; shift < WORD_BITS; shift <<= 1)
        value |= value >> shift;
    return value + 1;
}

enum {
    FLOAT_EXPONENT_SHIFT = 23,
    FLOAT_EXPONENT_BIAS = 127,
};

// The binary exponent of value after conversion to float (so rounding near powers of two counts).
uint32_t render_record_geometry_index_logarithm(uint32_t value)
{
    union {
        float number;
        uint32_t bits;
    } converted;
    converted.number = (float)value;
    return (converted.bits >> FLOAT_EXPONENT_SHIFT) - FLOAT_EXPONENT_BIAS;
}

enum {
    TUPLE_CLASSIFICATION_3 = 3,
    TUPLE_CLASSIFICATION_7 = 7,
    TUPLE_CLASSIFICATION_8 = 8,
    TUPLE_CLASSIFICATION_9 = 9,
    TUPLE_CLASSIFICATION_10 = 10,
    TUPLE_CLASSIFICATION_13 = 13,
    TUPLE_CLASSIFICATION_14 = 14,
    TUPLE_CLASSIFICATION_15 = 15,
    TUPLE_GROUP_FOUR = 4,
    TUPLE_GROUP_EIGHT = 8,
};

// The group of a tuple classification: 4 or 8 for the listed ones, 1 for every other. The
// original is a switch; the shared table the replacement reads is clang's lookup table for it.
uint32_t render_record_tuple_classification_group(uint32_t classification)
{
    switch (classification) {
    case TUPLE_CLASSIFICATION_3:
    case TUPLE_CLASSIFICATION_7:
    case TUPLE_CLASSIFICATION_9:
    case TUPLE_CLASSIFICATION_10:
    case TUPLE_CLASSIFICATION_13:
    case TUPLE_CLASSIFICATION_15:
        return TUPLE_GROUP_FOUR;
    case TUPLE_CLASSIFICATION_8:
    case TUPLE_CLASSIFICATION_14:
        return TUPLE_GROUP_EIGHT;
    default:
        return 1;
    }
}

enum {
    ALTERNATE_FIRST = 5,
    ALTERNATE_LAST_INDEX = 10,
    ALTERNATE_ADMITTED = 0x473,
};

// Whether a classification (5..15) belongs to the alternate tuple path's subset.
bool render_record_tuple_alternate_classification(uint32_t classification)
{
    uint32_t index = classification - ALTERNATE_FIRST;
    if (index <= ALTERNATE_LAST_INDEX)
        return (ALTERNATE_ADMITTED >> index) & 1;
    return false;
}

enum {
    GEOMETRY_FIELD_ENTRIES = 32,
    CATEGORY_SHIFT = 22,
    CATEGORY_MASK = 7,
    CLASSIFICATION_SHIFT = 2,
    CLASSIFICATION_MASK = 15,
    FIRST_EXPONENT_SHIFT = 11,
    FIRST_EXPONENT_MASK = 7,
    SECOND_EXPONENT_SHIFT = 25,
    SECOND_EXPONENT_MASK = 3,
};

// The packed field word of each geometry format.
extern const uint32_t render_geometry_fields[GEOMETRY_FIELD_ENTRIES];

// Bits 22..24 (the category) of a format's field word.
uint32_t render_record_geometry_category(uint32_t *output, uint32_t format)
{
    if (!output || format >= GEOMETRY_FIELD_ENTRIES)
        return GEOMETRY_UNSUPPORTED;
    *output = (render_geometry_fields[format] >> CATEGORY_SHIFT) & CATEGORY_MASK;
    return 0;
}

// Bits 2..5 (the classification) of a format's field word.
uint32_t render_record_geometry_classification(uint32_t *output, uint32_t format)
{
    if (!output || format >= GEOMETRY_FIELD_ENTRIES)
        return GEOMETRY_UNSUPPORTED;
    *output = (render_geometry_fields[format] >> CLASSIFICATION_SHIFT) & CLASSIFICATION_MASK;
    return 0;
}

// Bits 11..13 (the first exponent) of a format's field word.
uint32_t render_record_geometry_first_exponent(uint32_t *output, uint32_t format)
{
    if (!output || format >= GEOMETRY_FIELD_ENTRIES)
        return GEOMETRY_UNSUPPORTED;
    *output = (render_geometry_fields[format] >> FIRST_EXPONENT_SHIFT) & FIRST_EXPONENT_MASK;
    return 0;
}

// Bits 25..26 (the second exponent) of a format's field word.
uint32_t render_record_geometry_second_exponent(uint32_t *output, uint32_t format)
{
    if (!output || format >= GEOMETRY_FIELD_ENTRIES)
        return GEOMETRY_UNSUPPORTED;
    *output = (render_geometry_fields[format] >> SECOND_EXPONENT_SHIFT) & SECOND_EXPONENT_MASK;
    return 0;
}
