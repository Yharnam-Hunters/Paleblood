// SPDX-License-Identifier: GPL-2.0-or-later
// Resolve the packed parameter-table index. Keep the original validation order,
// repeated count predicate, and distinct wrapping scale operations.
#include <cstring>

#include "record_geometry_helpers.h"
#include "record_tuple.h"

extern "C" uint32_t bb_render_record_geometry_resolve_parameter_index(uint32_t *output, uint32_t word,
                                                                       uint32_t metadata, uint32_t count)
{
    constexpr uint32_t initial_count_limit = 8, later_count_limit = 16;
    constexpr uint32_t metadata_limit = 128;
    constexpr uint32_t scale_shift = 6, reduction_shift = 3;
    constexpr uint32_t dword_shift_mask = 31, qword_shift_mask = 63;
    constexpr uint32_t special_category = 2;
    constexpr uint32_t special_capacity = 64, minimum_capacity = 256, maximum_capacity = 1024;
    constexpr uint32_t minimum_scaled = 64, maximum_scaled = 4096;
    constexpr uint32_t alternate_index_offset = 8;

    if (output == nullptr) return render::record_geometry_unsupported;
    if (render::record_tuple_parameter_supported(count) == 0) return render::record_geometry_unsupported;
    if (count > initial_count_limit) return render::record_geometry_unsupported;

    uint32_t classification;
    uint32_t status = render::get_record_geometry_classification(&classification, word);
    if (status != 0) return status;
    if (!render::record_tuple_classification_supported(classification)) return render::record_geometry_unsupported;
    if (metadata == 0) return render::record_geometry_unsupported;
    if (metadata > metadata_limit) return render::record_geometry_unsupported;
    // The native second predicate is on count again, not on metadata.
    if (render::record_tuple_parameter_supported(count) == 0) return render::record_geometry_unsupported;
    if (count == 0) return render::record_geometry_unsupported;
    if (count > later_count_limit) return render::record_geometry_unsupported;

    uint32_t category, second_exponent, first_exponent;
    status = render::get_record_geometry_category(&category, word);
    if (status != 0) return status;
    status = render::get_record_geometry_second_exponent(&second_exponent, word);
    if (status != 0) return status;
    status = render::get_record_geometry_first_exponent(&first_exponent, word);
    if (status != 0) return status;

    const uint32_t group = render::record_tuple_classification_group(classification);
    uint32_t scaled = (metadata * group) << scale_shift;
    scaled >>= reduction_shift;
    const uint32_t shifted = scaled << (second_exponent & dword_shift_mask);
    uint32_t capacity;
    if (category == special_category) {
        const uint64_t wide_capacity = uint64_t(special_capacity) << (first_exponent & qword_shift_mask);
        capacity = static_cast<uint32_t>(wide_capacity);
    } else {
        capacity = shifted > minimum_capacity ? shifted : minimum_capacity;
    }
    if (capacity > maximum_capacity) capacity = maximum_capacity;
    scaled *= count;
    if (scaled >= capacity) scaled = capacity;

    if (render::record_tuple_parameter_supported(scaled) == 0) return render::record_geometry_unsupported;
    if (scaled < minimum_scaled) return render::record_geometry_unsupported;
    if (scaled > maximum_scaled) return render::record_geometry_unsupported;
    const uint32_t base_index = render::record_geometry_index_logarithm(scaled >> scale_shift);
    const bool alternate = render::record_tuple_alternate_classification(classification) != 0;
    const uint32_t index = alternate ? base_index + alternate_index_offset : base_index;
    std::memcpy(output, &index, sizeof(index));
    return 0;
}
