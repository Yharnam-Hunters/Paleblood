// SPDX-License-Identifier: GPL-2.0-or-later
// Record bit fields used by the builder. Masked setters return the preserved
// pre-write bits, not the stored word or a status. Names describe known offsets
// and widths without assigning a physical resource format.
#pragma once

#include <cstdint>

#include "record_constructor.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x01486520, record_set_word_0c_fields,
            uint32_t(RecordConstructorPrefix *record, uint32_t first, uint32_t second));
RT_ORIGINAL(0x014866e0, record_set_word_04_repeated_field,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x01486710, record_set_word_08_low22,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x01486760, record_set_word_14_second_low5_field,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x014867c0, record_set_word_10_byte_at_bit13,
            uint32_t(RecordConstructorPrefix *record, uint8_t value));
RT_ORIGINAL(0x014867e0, record_set_word_10_byte_at_bit14,
            uint32_t(RecordConstructorPrefix *record, uint8_t value));
RT_ORIGINAL(0x01486800, record_set_word_28_low22,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));

// Inline forms apply to observed builder uses; the original short entries stay
// unhooked. Store-only forms are logically void and do not preserve ambient RAX.
inline uint32_t record_read_word_14_low5(const RecordConstructorPrefix *record)
{
    constexpr uint32_t field_mask = 31;
    return record->pair_10.high.get() & field_mask;
}

inline void record_store_word_00(RecordConstructorPrefix *record, uint32_t value)
{
    record->pair_00.low.set(value);
}

inline void record_store_word_1c(RecordConstructorPrefix *record, uint32_t value)
{
    record->pair_18.high.set(value);
}

inline void record_store_word_24(RecordConstructorPrefix *record, uint32_t value)
{
    record->pair_20.high.set(value);
}

inline uint32_t record_read_word_00(const RecordConstructorPrefix *record)
{
    return record->pair_00.low.get();
}

inline uint32_t record_read_word_08_low22(const RecordConstructorPrefix *record)
{
    constexpr uint32_t field_mask = 0x003fffff;
    return record->pair_08.low.get() & field_mask;
}
}  // namespace render
