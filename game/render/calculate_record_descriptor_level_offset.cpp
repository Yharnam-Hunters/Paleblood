// SPDX-License-Identifier: GPL-2.0-or-later
// Accumulate every level through the requested one, then publish the two optional
// QWORDs in order. Inner statuses are deliberately ignored: the reused geometry
// result can retain its last partial total when a later calculation fails.
#include "record_descriptor_decoder.h"

#include <cstring>

extern "C" uint32_t bb_render_calculate_record_descriptor_level_offset(
    uint64_t *offset, uint64_t *total, const render::RecordDescriptorPrefix *record,
    uint32_t last_level, uint32_t index)
{
    using namespace render;
    constexpr uint32_t triple_kind = 10, flagged_kind = 11, flagged_multiplier = 6;
    constexpr uint32_t native_shift_mask = 31;
    if (record == nullptr || record_descriptor_level_count(record) < last_level)
        return record_geometry_unsupported;
    const uint32_t kind = record_descriptor_high_field(record);
    const uint32_t first = record_descriptor_first_field(record) + 1;
    const uint32_t second = record_descriptor_second_field(record) + 1;
    const uint32_t third = kind == triple_kind ? record_descriptor_third_field(record) + 1 : 1;

    // Preserve untouched scratch after a failed inner initialization.
    RecordGeometryDescriptor descriptor;
    initialize_record_level_descriptor(&descriptor, record, 0, index);
    RecordGeometryResult geometry{};
    uint32_t count_limit = record_descriptor_third_field(record) + 1;
    if (kind == flagged_kind) count_limit *= flagged_multiplier;
    else if (kind == triple_kind) count_limit = 1;
    if (record_descriptor_rounding_flag(record) != 0)
        count_limit = record_geometry_round_up_power_of_two(count_limit);
    if (count_limit <= index) return record_geometry_unsupported;

    uint64_t accumulated = 0, previous_total = 0;
    uint32_t level = 0;
    do {
        const uint64_t scaled_previous = previous_total * uint64_t(count_limit);
        const unsigned shift = level & native_shift_mask;
        uint32_t dimension = first >> shift;
        descriptor.first_08.set(dimension != 0 ? dimension : 1);
        dimension = second >> shift;
        descriptor.second_0c.set(dimension != 0 ? dimension : 1);
        dimension = third >> shift;
        descriptor.third_10.set(dimension != 0 ? dimension : 1);
        const uint32_t base = level != 0 ? record_descriptor_base_field(record) + 1 : 0;
        accumulated += scaled_previous;
        descriptor.base_18.set(base);
        descriptor.level_1c.set(level);
        calculate_record_geometry(&geometry, &descriptor);
        previous_total = read_geometry_total(geometry.total_10);
        ++level;
    } while (level <= last_level);

    if (offset != nullptr) {
        const uint64_t value = accumulated + uint64_t(index) * previous_total;
        std::memcpy(offset, &value, sizeof(value));
    }
    if (total != nullptr) std::memcpy(total, &previous_total, sizeof(previous_total));
    return 0;
}
