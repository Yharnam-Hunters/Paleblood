// SPDX-License-Identifier: GPL-2.0-or-later
// Choose the first constructor selector. Only the flag's low byte participates,
// and only the extended selector uses it; every other unlisted input returns zero.
#include "record_builder_selectors.h"

extern "C" uint32_t bb_render_choose_record_constructor_selector(uint32_t selector, uint32_t flag)
{
    constexpr uint32_t first_selector = 2, second_selector = 4, third_selector = 8;
    constexpr uint32_t extended_selector = 16;
    constexpr uint32_t first_index = 1, second_index = 2, third_index = 3, extended_index = 4;
    switch (selector) {
    case first_selector: return first_index;
    case second_selector: return second_index;
    case third_selector: return third_index;
    case extended_selector: return static_cast<uint8_t>(flag) != 0 ? extended_index : 0;
    default: return 0;
    }
}
