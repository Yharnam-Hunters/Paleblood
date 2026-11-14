// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the render record descriptor's top-word field accessors,
// its packed encoding reader and the user PA query wrapper.
#include <stdbool.h>
#include <stdint.h>

// The descriptor prefix: two pairs of 32-bit words.
typedef struct {
    uint32_t first_low;
    uint32_t first_high;
    uint32_t second_low;
    uint32_t second_high;
} RecordDescriptorPrefix;

#define HIGH_FIELD_KEEP 0x0fffffffu
#define HIGH_FIELD_SHIFT 28

#define EXPONENT_PARTITION 0xe0000000u
#define COUNT_SHIFT 16
#define COUNT_MASK 0xfu

enum {
    ENCODING_FIRST_SHIFT = 20,
    ENCODING_FIRST_MASK = 63,
    ENCODING_SECOND_SHIFT = 26,
    ENCODING_SECOND_MASK = 15,
    ENCODING_COMPONENT_MASK = 7,
    ENCODING_FOURTH_SHIFT = 3,
    ENCODING_FIFTH_SHIFT = 6,
    ENCODING_SIXTH_SHIFT = 9,
};

// The first parameter is passed as a full 32-bit value: only that matches (not uint8_t).
extern uint32_t compose_record_encoding_fields(uint32_t first, uint32_t second,
                                                      uint32_t third, uint32_t fourth,
                                                      uint32_t fifth, uint32_t sixth);
extern int native_is_user_pa_enabled(void);

// Store the top nibble of the second high word and return its old low 28 bits.
uint32_t render_record_descriptor_set_high_field(RecordDescriptorPrefix *descriptor, uint32_t value)
{
    uint32_t kept = descriptor->second_high & HIGH_FIELD_KEEP;
    descriptor->second_high = kept | (value << HIGH_FIELD_SHIFT);
    return kept;
}

// The count bits 16..19, only when the top three bits of the second high word are all set.
uint32_t render_record_descriptor_count_exponent(const RecordDescriptorPrefix *record)
{
    uint32_t word = record->second_high;
    if (word < EXPONENT_PARTITION)
        return 0;
    return (word >> COUNT_SHIFT) & COUNT_MASK;
}

// Unpack six encoding fields from the two high words and recompose the encoding.
uint32_t render_record_descriptor_encoding(const RecordDescriptorPrefix *record)
{
    uint32_t first = record->first_high;
    uint32_t second = record->second_high;
    return compose_record_encoding_fields(
        (first >> ENCODING_FIRST_SHIFT) & ENCODING_FIRST_MASK,
        (first >> ENCODING_SECOND_SHIFT) & ENCODING_SECOND_MASK, second & ENCODING_COMPONENT_MASK,
        (second >> ENCODING_FOURTH_SHIFT) & ENCODING_COMPONENT_MASK,
        (second >> ENCODING_FIFTH_SHIFT) & ENCODING_COMPONENT_MASK,
        (second >> ENCODING_SIXTH_SHIFT) & ENCODING_COMPONENT_MASK);
}

// Whether the system reports user PA as enabled.
bool render_record_step_user_pa_enabled(void)
{
    return native_is_user_pa_enabled() != 0;
}
