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

inline uint32_t record_descriptor_third_field(const RecordDescriptorPrefix *record)
{
    constexpr uint32_t third_mask = 0x1fff;
    return record->pair_10.low.get() & third_mask;
}

}  // namespace render
