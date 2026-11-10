// SPDX-License-Identifier: GPL-2.0-or-later
// Extract the second two-bit exponent used by the geometry arithmetic. Invalid
// indices and NULL output reject before the table or output is accessed.
#include <cstring>

#include "record_geometry_helpers.h"

extern "C" uint32_t bb_render_record_geometry_second_exponent(uint32_t *output, uint32_t word)
{
    constexpr uint32_t exponent_shift = 25;
    constexpr uint32_t exponent_mask = 3;
    if (output == nullptr || word >= render::record_geometry_field_entries)
        return render::record_geometry_unsupported;
    const uint32_t exponent = (render::record_geometry_fields.get()[word] >> exponent_shift) & exponent_mask;
    std::memcpy(output, &exponent, sizeof(exponent));
    return 0;
}
