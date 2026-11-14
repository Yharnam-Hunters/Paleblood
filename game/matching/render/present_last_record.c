// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for two setters of the present path's last record.
#include <stdint.h>

#define INDICES_KEEP 0xff001800u
#define FIRST_INDEX_MASK 0x7ffu
#define SECOND_INDEX_SHIFT 13
#define SECOND_INDEX_MASK 0x00ffe000u

#define DEPTH_COMPRESS_DISABLE_SHIFT 6
#define DEPTH_COMPRESS_DISABLE_BIT (1u << DEPTH_COMPRESS_DISABLE_SHIFT)

typedef struct {
    uint8_t unknown_0x00[0x20];
    uint32_t indices;
} PresentLastRecord;

// Two 11-bit indices: bits 0..10 and 13..23 of the word at 0x20. Returns the kept bits.
uint32_t render_set_present_last_record_indices(PresentLastRecord *record, uint32_t first, uint32_t second)
{
    uint32_t kept = record->indices & INDICES_KEEP;
    record->indices = kept | (first & FIRST_INDEX_MASK) | ((second << SECOND_INDEX_SHIFT) & SECOND_INDEX_MASK);
    return kept;
}

// Bit 6 of the control word: the low bit of `disable`. Returns the kept bits.
uint32_t render_set_present_last_record_depth_compress_disable(uint32_t *control, uint32_t disable)
{
    uint32_t kept = *control & ~DEPTH_COMPRESS_DISABLE_BIT;
    *control = kept | ((disable & 1) << DEPTH_COMPRESS_DISABLE_SHIFT);
    return kept;
}
