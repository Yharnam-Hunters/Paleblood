// SPDX-License-Identifier: GPL-2.0-or-later
// Masked record setters used by presentation construction. Each returns the
// preserved old bits. Auxiliary setters shift the full input byte after clearing
// the named flag bit, so they deliberately do not normalize the byte to bool.
#pragma once
#include "record_constructor_layout.h"
#include "present_record_range_call.h"
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x014866e0, set_present_record_pitch_tile_max,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x01486710, set_present_record_slice_tile_max,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x01486760, set_present_record_fmask_tile_mode_index,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));
RT_ORIGINAL(0x014867c0, record_set_first_auxiliary_flag,
            uint32_t(RecordConstructorPrefix *record, uint8_t value));
RT_ORIGINAL(0x014867e0, record_set_second_auxiliary_flag,
            uint32_t(RecordConstructorPrefix *record, uint8_t value));
RT_ORIGINAL(0x01486800, set_present_record_fmask_tile_max,
            uint32_t(RecordConstructorPrefix *record, uint32_t value));

}  // namespace render
