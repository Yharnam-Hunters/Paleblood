// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for small render record and register-word setters and
// readers. Each setter keeps the bits outside its field, stores the new field and returns the
// kept bits.
#include <stdbool.h>
#include <stdint.h>

#define STENCIL_COMPRESS_DISABLE_SHIFT 5
#define STENCIL_COMPRESS_DISABLE_BIT (1u << STENCIL_COMPRESS_DISABLE_SHIFT)

#define REGISTER_RANGE_KEEP 0xffffff8bu
#define REGISTER_RANGE_FIRST_SHIFT 2
#define REGISTER_RANGE_FIRST_MASK 0x04u
#define REGISTER_RANGE_LAST_SHIFT 4
#define REGISTER_RANGE_LAST_MASK 0x70u

#define REGISTER_ENABLED_KEEP 0xfffffffdu
#define REGISTER_ENABLED_SHIFT 1

#define SECOND_AUXILIARY_KEEP 0xffffbfffu
#define SECOND_AUXILIARY_SHIFT 14

#define WORD_28_LOW22_KEEP 0xffc00000u
#define WORD_28_LOW22_MASK 0x003fffffu

#define MAPPING_FIRST_SHIFT 2
#define MAPPING_FIRST_MASK 0x1fu
#define MAPPING_SECOND_SHIFT 8
#define MAPPING_SECOND_MASK 0x07u
#define MAPPING_MODE_SHIFT 11
#define MAPPING_MODE_MASK 0x03u

typedef struct {
    uint32_t word_00;
    uint32_t word_04;
    uint32_t word_08;
    uint32_t word_0c;
    uint32_t word_10;
    uint32_t word_14;
    uint32_t word_18;
    uint32_t word_1c;
    uint32_t word_20;
    uint32_t word_24;
    uint32_t word_28;
} RecordPrefix;

// Packs the three mapping fields into one mapping word.
extern uint64_t render_pack_record_mapping_word(uint32_t first, uint32_t second, uint32_t mode);

// Bit 5 of the present path's last-record control word.
uint32_t render_set_present_last_record_stencil_compress_disable(uint32_t *control, uint32_t disabled)
{
    uint32_t kept = *control & ~STENCIL_COMPRESS_DISABLE_BIT;
    *control = kept | ((disabled << STENCIL_COMPRESS_DISABLE_SHIFT) & STENCIL_COMPRESS_DISABLE_BIT);
    return kept;
}

// A register word's range: bit 2 from `first` and bits 4..6 from `last`.
uint32_t render_set_record_register_range(uint32_t *word, uint32_t first, uint32_t last)
{
    uint32_t kept = *word & REGISTER_RANGE_KEEP;
    *word = kept | ((first << REGISTER_RANGE_FIRST_SHIFT) & REGISTER_RANGE_FIRST_MASK)
          | ((last << REGISTER_RANGE_LAST_SHIFT) & REGISTER_RANGE_LAST_MASK);
    return kept;
}

// Bit 1 of a register word: the enabled flag (a C++ bool).
uint32_t render_set_record_register_enabled(uint32_t *word, bool enabled)
{
    uint32_t kept = *word & REGISTER_ENABLED_KEEP;
    *word = kept | ((uint32_t)enabled << REGISTER_ENABLED_SHIFT);
    return kept;
}

// Bit 14 of word 0x10 (a C++ bool).
uint32_t render_record_set_second_auxiliary_flag(RecordPrefix *record, bool enabled)
{
    uint32_t kept = record->word_10 & SECOND_AUXILIARY_KEEP;
    record->word_10 = kept | ((uint32_t)enabled << SECOND_AUXILIARY_SHIFT);
    return kept;
}

// Bits 0..21 of word 0x28.
uint32_t render_record_set_word_28_low22(RecordPrefix *record, uint32_t value)
{
    uint32_t kept = record->word_28 & WORD_28_LOW22_KEEP;
    record->word_28 = kept | (value & WORD_28_LOW22_MASK);
    return kept;
}

// Unpacks the three fields of word 0x10 and packs them into a mapping word.
uint64_t render_read_record_mapping_word(const RecordPrefix *record)
{
    uint32_t word = record->word_10;
    return render_pack_record_mapping_word((word >> MAPPING_FIRST_SHIFT) & MAPPING_FIRST_MASK,
                                           (word >> MAPPING_SECOND_SHIFT) & MAPPING_SECOND_MASK,
                                           (word >> MAPPING_MODE_SHIFT) & MAPPING_MODE_MASK);
}
