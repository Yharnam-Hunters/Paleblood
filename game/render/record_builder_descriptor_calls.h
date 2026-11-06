// SPDX-License-Identifier: GPL-2.0-or-later
// Locate a selected descriptor element after accumulating preceding levels.
#pragma once
#include "record_descriptor_offsets.h"
namespace render {
RT_ORIGINAL(0x01488370, record_descriptor_set_high_field,
            uint32_t(RecordDescriptorPrefix *descriptor, uint32_t value));
RT_ORIGINAL(0x014a2c40, calculate_record_descriptor_level_offset,
            uint32_t(uint64_t *offset, uint64_t *total, const RecordDescriptorPrefix *record,
                     uint32_t level, uint32_t index));
}  // namespace render
