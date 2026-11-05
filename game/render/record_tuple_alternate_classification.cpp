// SPDX-License-Identifier: GPL-2.0-or-later
// Recognize the bounded classification subset used by the alternate tuple path.
#include <cstdint>

extern "C" uint32_t bb_render_record_tuple_alternate_classification(uint32_t classification)
{
    constexpr uint32_t first_classification = 5;
    constexpr uint32_t last_index = 10;
    constexpr uint32_t admitted_classifications = 0x473;
    const uint32_t index = classification - first_classification;
    if (index > last_index) return 0;
    return (admitted_classifications >> index) & 1;
}
