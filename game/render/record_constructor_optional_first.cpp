// SPDX-License-Identifier: GPL-2.0-or-later
// Align a pair, then advance the second component until the selected product
// bits clear. The optional outputs publish in order and may overlap.
#include <cstring>

#include "record_constructor_helpers.h"

extern "C" uint32_t bb_render_record_constructor_optional_first(uint64_t *total, uint32_t *unit,
                                                               uint32_t *first, uint32_t *second,
                                                               uint32_t first_value, uint32_t second_value,
                                                               uint32_t count, uint8_t flag)
{
    constexpr uint32_t compact_alignment = 64;
    constexpr uint32_t default_first_alignment = 512, default_second_alignment = 256;
    constexpr uint32_t invalid_calculation = 0x80d30001;
    constexpr uint32_t product_shift = 7, stop_bits = 11, quantity_bits = 55;
    constexpr uint64_t stop_mask = (uint64_t(1) << stop_bits) - 1;
    constexpr uint64_t quantity_mask = (uint64_t(1) << quantity_bits) - 1;
    constexpr uint32_t published_unit = 0x800;

    // The zero-flag path's constant-only native loop resolves to these two
    // alignments. The low-byte flag selects the separate 64/64 path.
    const uint32_t first_alignment = flag != 0 ? compact_alignment : default_first_alignment;
    const uint32_t second_alignment = flag != 0 ? compact_alignment : default_second_alignment;
    if (render::record_tuple_parameter_supported(first_alignment) == 0) return invalid_calculation;
    if (render::record_tuple_parameter_supported(second_alignment) == 0) return invalid_calculation;
    const uint32_t multiplier = count;

    const uint32_t aligned_first = (first_value + first_alignment - 1) & (0u - first_alignment);
    uint32_t next_second = (second_value + second_alignment - 1) & (0u - second_alignment);
    uint64_t product;
    do {
        product = uint64_t(next_second) * aligned_first;
        next_second += second_alignment;
    } while (((product >> product_shift) & stop_mask) != 0);

    if (total != nullptr) {
        const uint64_t quantity = (product >> product_shift) & quantity_mask;
        const uint64_t value = uint64_t(multiplier) * quantity;
        std::memcpy(total, &value, sizeof(value));
    }
    if (unit != nullptr) std::memcpy(unit, &published_unit, sizeof(published_unit));
    if (first != nullptr) std::memcpy(first, &aligned_first, sizeof(aligned_first));
    if (second != nullptr) {
        // The product belongs to the row before the final unconditional advance.
        const uint32_t selected_second = next_second - second_alignment;
        std::memcpy(second, &selected_second, sizeof(selected_second));
    }
    return 0;
}
