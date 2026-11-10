// SPDX-License-Identifier: GPL-2.0-or-later
// Read the three-bit category of a bounded record word. Rejection leaves the
// borrowed output untouched; success publishes one DWORD after the table read.
#include <cstring>

#include "record_geometry_helpers.h"

extern "C" uint32_t bb_render_record_geometry_category(uint32_t *output, uint32_t word)
{
    constexpr uint32_t category_shift = 22;
    constexpr uint32_t category_mask = 7;
    if (output == nullptr || word >= render::record_geometry_field_entries)
        return render::record_geometry_unsupported;
    const uint32_t category = (render::record_geometry_fields.get()[word] >> category_shift) & category_mask;
    std::memcpy(output, &category, sizeof(category));
    return 0;
}
