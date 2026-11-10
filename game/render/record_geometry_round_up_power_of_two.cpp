// SPDX-License-Identifier: GPL-2.0-or-later
// Round upward in the uint32_t domain. Zero and values whose next power
// exceeds 32 bits deliberately wrap to zero rather than saturating.
#include <cstdint>
#include <limits>

extern "C" uint32_t bb_render_record_geometry_round_up_power_of_two(uint32_t value)
{
    uint32_t spread = value - 1;
    // Double the covered bit range until every bit below the highest one is set.
    for (uint32_t covered_bits = 1; covered_bits < std::numeric_limits<uint32_t>::digits;
         covered_bits <<= 1)
        spread |= spread >> covered_bits;
    return spread + 1;
}
