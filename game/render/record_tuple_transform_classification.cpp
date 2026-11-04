// SPDX-License-Identifier: GPL-2.0-or-later
// Transform classifications after scaling metadata by their original group.
// Multiplication and the left shift wrap at 32 bits before the threshold tests.
#include "record_tuple.h"

namespace {
// Rows cover classifications 3 through 15. Only admitted classifications reach
// these tables; the upper threshold changes the results for classifications 8/14.
constexpr uint32_t below_upper_threshold[render::record_tuple_group_entries] = {
    3, 4, 5, 6, 4, 7, 5, 6, 11, 12, 12, 13, 11,
};
constexpr uint32_t at_or_above_upper_threshold[render::record_tuple_group_entries] = {
    3, 4, 5, 6, 4, 4, 5, 6, 11, 12, 12, 12, 11,
};
}  // namespace

extern "C" uint32_t bb_render_record_tuple_transform_classification(uint32_t classification,
                                                                    uint32_t metadata)
{
    constexpr uint32_t admitted_classifications = 0x1cf1;
    constexpr uint32_t scale_shift = 6;
    constexpr uint32_t transformation_threshold = 0x2001;
    constexpr uint32_t upper_threshold = 0x4010;
    const uint32_t index = classification - render::record_tuple_first_classification;
    if (index >= render::record_tuple_group_entries || ((admitted_classifications >> index) & 1) == 0)
        return classification;
    const uint32_t scaled = (metadata * render::record_tuple_groups.get()[index]) << scale_shift;
    if (scaled < transformation_threshold) return classification;
    const auto &transformed = scaled < upper_threshold
        ? below_upper_threshold : at_or_above_upper_threshold;
    return transformed[index];
}
