// SPDX-License-Identifier: GPL-2.0-or-later
// Classifications outside the bounded original group table belong to group one.
#include "record_tuple.h"

extern "C" uint32_t bb_render_record_tuple_classification_group(uint32_t classification)
{
    constexpr uint32_t fallback_group = 1;
    const uint32_t index = classification - render::record_tuple_first_classification;
    if (index >= render::record_tuple_group_entries) return fallback_group;
    return render::record_tuple_groups.get()[index];
}
