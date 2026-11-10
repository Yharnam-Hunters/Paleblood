// SPDX-License-Identifier: GPL-2.0-or-later
// Combine the normalization parameters using only the flag's low byte. The
// enabled path bounds the second value before any original-table read.
#include "record_builder_selectors.h"

extern "C" uint32_t bb_render_combine_record_parameters(uint32_t first, uint32_t second, uint32_t flag)
{
    constexpr uint32_t first_selector = 2, second_selector = 4, third_selector = 8;
    constexpr uint32_t extended_selector = 16, first_limit = 2, second_limit = 3;
    constexpr uint32_t first_index = 1, second_index = 2, third_index = 3;
    if (static_cast<uint8_t>(flag) == 0) {
        switch (first) {
        case first_selector: return first_index;
        case second_selector: return second_index;
        case third_selector:
        case extended_selector: return third_index;
        default: return 0;
        }
    }
    switch (first) {
    case first_selector: return second < first_limit ? second : 0;
    case second_selector: return second < second_limit ? second : 0;
    case third_selector:
    case extended_selector:
        if (second >= render::record_builder_selector_entries) return 0;
        return render::record_builder_secondary_values.get()[second];
    default: return 0;
    }
}
