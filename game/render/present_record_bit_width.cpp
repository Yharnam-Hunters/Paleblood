// SPDX-License-Identifier: GPL-2.0-or-later
// The present-source count fallback uses this three-component bit width with third=1.
// Other callers may supply any 32-bit components; all zero produces zero.
#include <bit>
#include <cstdint>

extern "C" uint32_t bb_render_present_record_bit_width(uint32_t first, uint32_t second,
                                                       uint32_t third)
{
    return static_cast<uint32_t>(std::bit_width(first | second | third));
}
