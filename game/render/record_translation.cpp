// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the two selector translations that record normalization calls (record_normalization.h).
// Each sorts a selector code into one of three groups, stores the group's value and returns 0;
// any other code stores 0 and returns the unsupported status 12. Codes are signed, so negative
// ones are unsupported.
// - render_translate_record_secondary_02ab7720: 0x13-0x16 and 0x27-0x2b store 3, 0x35-0x3b store 1;
//   0x2c-0x34 are unsupported.
// - render_translate_record_tertiary_02ab78a0: 0x13-0x16 store 1; 0x27-0x2b and 0x35-0x3b store 0;
//   0x2c-0x34 are unsupported.
#include <cstdint>

namespace {

constexpr uint32_t translated = 0;
constexpr uint32_t unsupported = 12;

enum class SelectorGroup { low, middle, gap, high, other };

constexpr SelectorGroup group_of(int32_t selector)
{
    constexpr int32_t low_first = 0x13, low_last = 0x16;
    constexpr int32_t middle_first = 0x27, middle_last = 0x2b;
    constexpr int32_t gap_last = 0x34;
    constexpr int32_t high_last = 0x3b;
    if (selector >= low_first && selector <= low_last) return SelectorGroup::low;
    if (selector >= middle_first && selector <= middle_last) return SelectorGroup::middle;
    if (selector > middle_last && selector <= gap_last) return SelectorGroup::gap;
    if (selector > gap_last && selector <= high_last) return SelectorGroup::high;
    return SelectorGroup::other;
}

uint32_t store(uint32_t *value, uint32_t result, uint32_t status)
{
    *value = result;
    return status;
}

}  // namespace

extern "C" uint32_t bb_render_translate_record_secondary_02ab7720(uint32_t *value, int32_t selector)
{
    constexpr uint32_t low_or_middle = 3, high = 1;
    switch (group_of(selector)) {
    case SelectorGroup::low:
    case SelectorGroup::middle: return store(value, low_or_middle, translated);
    case SelectorGroup::high: return store(value, high, translated);
    default: return store(value, 0, unsupported);
    }
}

extern "C" uint32_t bb_render_translate_record_tertiary_02ab78a0(uint32_t *value, int32_t selector)
{
    constexpr uint32_t low = 1;
    switch (group_of(selector)) {
    case SelectorGroup::low: return store(value, low, translated);
    case SelectorGroup::middle:
    case SelectorGroup::high: return store(value, 0, translated);
    default: return store(value, 0, unsupported);
    }
}
