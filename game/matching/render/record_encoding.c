// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the packed record-encoding word: composing it and
// reading its category and component fields.
#include <stdbool.h>
#include <stdint.h>

// The encoding word: an 8-bit format, a 4-bit category nibble (bits 8..11) and four 3-bit
// components from bit 12.
#define FORMAT_MASK 0xffu
#define FORMAT_COUNT 59
#define CATEGORY_SHIFT 8
#define CATEGORY_MASK 0xfu
#define FIRST_COMPONENT_SHIFT 12
#define SECOND_COMPONENT_SHIFT 15
#define THIRD_COMPONENT_SHIFT 18
#define FOURTH_COMPONENT_SHIFT 21
#define COMPONENT_MASK 7u
#define COMPONENT_COUNT 4
#define INVALID_COMPONENT 2

// The metadata word of each format.
extern const uint32_t render_record_encoding_metadata_table[FORMAT_COUNT];

// Category nibbles with a category, and the category of each.
enum {
    NIBBLE_CATEGORY_0 = 0,
    NIBBLE_CATEGORY_1 = 1,
    NIBBLE_CATEGORY_4 = 4,
    NIBBLE_CATEGORY_5 = 5,
    NIBBLE_CATEGORY_7 = 7,
    NIBBLE_CATEGORY_6 = 9,
};
enum {
    CATEGORY_0 = 0,
    CATEGORY_1 = 1,
    CATEGORY_4 = 4,
    CATEGORY_5 = 5,
    CATEGORY_6 = 6,
    CATEGORY_7 = 7,
};

// Packs the format byte, the category nibble and the four components into one encoding word.
// The order of the terms is the original's: clang schedules them as written.
uint32_t render_compose_record_encoding_fields(uint32_t format, uint32_t category, uint32_t first,
                                               uint32_t second, uint32_t third, uint32_t fourth)
{
    return ((category & CATEGORY_MASK) << CATEGORY_SHIFT) | (uint8_t)format
         | ((first & COMPONENT_MASK) << FIRST_COMPONENT_SHIFT)
         | ((second & COMPONENT_MASK) << SECOND_COMPONENT_SHIFT)
         | ((fourth & COMPONENT_MASK) << FOURTH_COMPONENT_SHIFT)
         | ((third & COMPONENT_MASK) << THIRD_COMPONENT_SHIFT);
}

// One of the four 3-bit components; 2 for a component number past the last.
uint32_t render_read_record_descriptor_component(const uint32_t *encoding, uint32_t component)
{
    if (component >= COMPONENT_COUNT)
        return INVALID_COMPONENT;
    uint32_t word = *encoding;
    uint32_t components[COMPONENT_COUNT] = {
        (word >> FIRST_COMPONENT_SHIFT) & COMPONENT_MASK,
        (word >> SECOND_COMPONENT_SHIFT) & COMPONENT_MASK,
        (word >> THIRD_COMPONENT_SHIFT) & COMPONENT_MASK,
        (word >> FOURTH_COMPONENT_SHIFT) & COMPONENT_MASK,
    };
    return components[component];
}

// Stores a category through `output` when it is given; always true.
static inline bool store_category(uint32_t *output, uint32_t category)
{
    if (output)
        *output = category;
    return true;
}

// The category of an encoding word's nibble (bits 8..11): false for a nibble with none.
bool render_record_encoding_category(const uint32_t *encoding, uint32_t *output)
{
    switch ((*encoding >> CATEGORY_SHIFT) & CATEGORY_MASK) {
    case NIBBLE_CATEGORY_0:
        return store_category(output, CATEGORY_0);
    case NIBBLE_CATEGORY_1:
        return store_category(output, CATEGORY_1);
    case NIBBLE_CATEGORY_4:
        return store_category(output, CATEGORY_4);
    case NIBBLE_CATEGORY_5:
        return store_category(output, CATEGORY_5);
    case NIBBLE_CATEGORY_7:
        return store_category(output, CATEGORY_7);
    case NIBBLE_CATEGORY_6:
        return store_category(output, CATEGORY_6);
    default:
        return false;
    }
}

// The metadata word of an encoding word's format byte; 0 for a format past the table.
uint32_t render_record_encoding_metadata(const uint32_t *encoding)
{
    uint32_t format = *encoding & FORMAT_MASK;
    if (format >= FORMAT_COUNT)
        return 0;
    return render_record_encoding_metadata_table[format];
}
