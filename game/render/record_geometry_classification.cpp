// SPDX-License-Identifier: GPL-2.0-or-later
// Read the four-bit classification of a bounded record word. The original table
// is sampled before the optional caller storage can alias and overwrite it.
#include <cstring>

#include "record_geometry_helpers.h"

extern "C" uint32_t bb_render_record_geometry_classification(uint32_t *output, uint32_t word)
{
    constexpr uint32_t classification_shift = 2;
    constexpr uint32_t classification_mask = 15;
    if (output == nullptr || word >= render::record_geometry_field_entries)
        return render::record_geometry_unsupported;
    const uint32_t classification =
        (render::record_geometry_fields.get()[word] >> classification_shift) & classification_mask;
    std::memcpy(output, &classification, sizeof(classification));
    return 0;
}
