// SPDX-License-Identifier: GPL-2.0-or-later
// Read the selected scale from the original four-DWORD table. Invalid unsigned
// indices return zero before any table access, including negative bit patterns.
#include "record_builder_selectors.h"

extern "C" uint32_t bb_render_reduce_record_parameter(uint32_t value)
{
    if (value >= render::record_builder_selector_entries) return 0;
    return render::record_builder_scale_values.get()[value];
}
