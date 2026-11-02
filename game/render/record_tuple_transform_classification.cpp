// SPDX-License-Identifier: GPL-2.0-or-later
// Transform selected classifications after scaling metadata by their original
// group. Multiplication and the following shift wrap at 32 bits before comparison.
#include "record_tuple.h"

namespace {
constexpr uint32_t tuple_code_3 = 3;
constexpr uint32_t tuple_code_4 = 4;
constexpr uint32_t tuple_code_7 = 7;
constexpr uint32_t tuple_code_8 = 8;
constexpr uint32_t tuple_code_5 = 5;
constexpr uint32_t tuple_code_9 = 9;
constexpr uint32_t tuple_code_6 = 6;
constexpr uint32_t tuple_code_10 = 10;
constexpr uint32_t tuple_code_12 = 12;
constexpr uint32_t tuple_code_13 = 13;
constexpr uint32_t tuple_code_14 = 14;
constexpr uint32_t tuple_code_11 = 11;
constexpr uint32_t tuple_code_15 = 15;
}  // namespace


extern "C" uint32_t bb_render_record_tuple_transform_classification(uint32_t classification, uint32_t metadata)
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
    switch (classification) {
    case tuple_code_3: return tuple_code_3;
    case tuple_code_7: return tuple_code_4;
    case tuple_code_8: return scaled < upper_threshold ? tuple_code_7 : tuple_code_4;
    case tuple_code_9: return tuple_code_5;
    case tuple_code_10: return tuple_code_6;
    case tuple_code_13: return tuple_code_12;
    case tuple_code_14: return scaled < upper_threshold ? tuple_code_13 : tuple_code_12;
    case tuple_code_15: return tuple_code_11;
    default: return classification;
    }
}
