// SPDX-License-Identifier: GPL-2.0-or-later
// The original clears the lowest set bit in EAX, then overwrites only AL
// with the predicate. AL is Boolean; bits 8..31 of the full result need not be.
#include <cstdint>

extern "C" uint64_t bb_render_record_tuple_parameter_supported(uint32_t parameter)
{
    if (parameter == 0) {
        return 0;
    }

    constexpr uint32_t result_low_byte_mask = 0xffu;
    const uint32_t r = parameter & (parameter - 1u);
    const uint32_t result = (r & ~result_low_byte_mask) | static_cast<uint32_t>(r == 0);
    return static_cast<uint64_t>(result);
}
