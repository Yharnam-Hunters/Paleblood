// SPDX-License-Identifier: GPL-2.0-or-later
// The family sorts_16_byte_elements_with_comparator (symbols/families.csv, docs/FAMILIES.md):
// copies of one quicksort over the 16-byte elements first..last (inclusive indexes) of an array,
// ordered by a caller's comparator that says whether its first element goes before its second.
// The pivot is a copy of the middle element, taken before the partition; the partition moves i up
// past the elements before the pivot and j down past those after it, and swaps the two elements
// when i is still at or below j (an element with itself is left alone). The left part is sorted by
// a call to the same function; the right part by the loop, while i is below last.
#include <cstdint>

namespace {

// An element: 16 bytes the comparator alone interprets.
struct Element {
    uint8_t unknown_0x00[0x10];
};
static_assert(sizeof(Element) == 0x10);

using Before = bool(const Element *first, const Element *second);

}  // namespace

extern "C" void bb_engine_sorts_16_byte_elements_with_comparator(Element *elements, int32_t first, int32_t last,
                                                                  Before *before)
{
    for (;;) {
        // The middle index wraps as the code's 32-bit sum does.
        const int32_t middle = static_cast<int32_t>(static_cast<uint32_t>(first) + static_cast<uint32_t>(last)) >> 1;
        const Element pivot = elements[middle];
        int32_t i = first;
        int32_t j = last;
        do {
            while (before(&elements[i], &pivot))
                ++i;
            while (before(&pivot, &elements[j]))
                --j;
            if (i <= j) {
                if (i != j) {
                    const Element held = elements[j];
                    elements[j] = elements[i];
                    elements[i] = held;
                }
                ++i;
                --j;
            }
        } while (i <= j);
        if (first < j)
            bb_engine_sorts_16_byte_elements_with_comparator(elements, first, j, before);
        if (i >= last)
            return;
        first = i;
    }
}
