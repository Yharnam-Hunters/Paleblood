// SPDX-License-Identifier: GPL-2.0-or-later
// Update the two masked range fields of a record prefix and return preserved old bits.
#pragma once

#include "record_constructor_layout.h"
#include "runtime/original.h"

namespace render::record_calls {
RT_ORIGINAL(0x01486520, set_present_record_range,
            uint32_t(RecordConstructorPrefix *record, uint32_t first, uint32_t last));
}  // namespace render::record_calls
