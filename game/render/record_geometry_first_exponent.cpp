// SPDX-License-Identifier: GPL-2.0-or-later
// Extract the first three-bit exponent used by the geometry arithmetic. This
// name describes its consumer, without assigning a hardware resource meaning.
#include <cstring>

#include "record_geometry_helpers.h"

extern "C" uint32_t bb_render_record_geometry_first_exponent(uint32_t *output, uint32_t word)
{
    constexpr uint32_t exponent_shift = 11;
    constexpr uint32_t exponent_mask = 7;
    if (output == nullptr || word >= render::record_geometry_field_entries)
        return render::record_geometry_unsupported;
    const uint32_t exponent = (render::record_geometry_fields.get()[word] >> exponent_shift) & exponent_mask;
    std::memcpy(output, &exponent, sizeof(exponent));
    return 0;
}
