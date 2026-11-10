// SPDX-License-Identifier: GPL-2.0-or-later
// Split one resolved parameter word into four optional two-bit outputs. Capture
// the word once, then preserve output order even when the borrowed outputs alias.
#include <cstring>

#include "record_geometry_helpers.h"

extern "C" uint32_t bb_render_record_geometry_parameters(uint32_t word, uint32_t metadata, uint32_t count,
                                                         uint32_t *first, uint32_t *second,
                                                         uint32_t *third, uint32_t *fourth)
{
    constexpr uint32_t field_mask = 3;
    constexpr uint32_t second_shift = 2, third_shift = 4, fourth_shift = 6;
    uint32_t index;
    const uint32_t status = render::record_geometry_resolve_parameter_index(&index, word, metadata, count);
    if (status != 0) return status;
    // The resolver's successful index is 0..4 or 8..12; NULL outputs do not skip this read.
    const uint32_t packed = render::record_geometry_parameter_fields.get()[index];
    if (first != nullptr) {
        const uint32_t value = packed & field_mask;
        std::memcpy(first, &value, sizeof(value));
    }
    if (second != nullptr) {
        const uint32_t value = (packed >> second_shift) & field_mask;
        std::memcpy(second, &value, sizeof(value));
    }
    if (third != nullptr) {
        const uint32_t value = (packed >> third_shift) & field_mask;
        std::memcpy(third, &value, sizeof(value));
    }
    if (fourth != nullptr) {
        const uint32_t value = (packed >> fourth_shift) & field_mask;
        std::memcpy(fourth, &value, sizeof(value));
    }
    return status;
}
