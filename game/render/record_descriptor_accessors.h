// SPDX-License-Identifier: GPL-2.0-or-later
// Packed descriptor fields consumed by the level helpers. Inline getters retain
// the original extraction widths and wrapping arithmetic.
#pragma once

#include <cstdint>

#include "record_descriptor.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x01489910, record_descriptor_level_count, uint32_t(const RecordDescriptorPrefix *record));

inline uint32_t record_descriptor_high_field(const RecordDescriptorPrefix *record)
{
    constexpr unsigned high_shift = 28;
    return record->pair_08.high.get() >> high_shift;
}

inline uint32_t record_descriptor_base_field(const RecordDescriptorPrefix *record)
{
    constexpr unsigned base_shift = 13;
    constexpr uint32_t base_mask = 0x3fff;
    return (record->pair_10.low.get() >> base_shift) & base_mask;
}

inline uint32_t record_descriptor_first_field(const RecordDescriptorPrefix *record)
{
    constexpr uint32_t first_mask = 0x3fff;
    return record->pair_08.low.get() & first_mask;
}

inline uint32_t record_descriptor_second_field(const RecordDescriptorPrefix *record)
{
    constexpr unsigned second_shift = 14;
    constexpr uint32_t second_mask = 0x3fff;
    return (record->pair_08.low.get() >> second_shift) & second_mask;
}

inline uint32_t record_descriptor_third_field(const RecordDescriptorPrefix *record)
{
    constexpr uint32_t third_mask = 0x1fff;
    return record->pair_10.low.get() & third_mask;
}

inline uint32_t record_descriptor_classification(const RecordDescriptorPrefix *record)
{
    constexpr unsigned classification_shift = 20;
    constexpr uint32_t classification_mask = 31;
    return (record->pair_08.high.get() >> classification_shift) & classification_mask;
}

inline uint8_t record_descriptor_rounding_flag(const RecordDescriptorPrefix *record)
{
    constexpr unsigned high_byte = 3, flag_shift = 1;
    constexpr uint8_t flag_mask = 2;
    return static_cast<uint8_t>((record->pair_08.high.bytes[high_byte] & flag_mask) >> flag_shift);
}

inline uint32_t record_encoding_byte_is_one(const uint32_t *encoding)
{
    return *reinterpret_cast<const uint8_t *>(encoding) == 1 ? 1 : 0;
}
}  // namespace render
