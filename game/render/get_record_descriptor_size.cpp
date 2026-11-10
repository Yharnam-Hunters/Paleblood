// SPDX-License-Identifier: GPL-2.0-or-later
// Accumulate descriptor geometry while retaining live borrowed-output updates.
// A failed decoder can leave scratch fields untouched; preserve those bytes
// instead of clearing the entire descriptor.
#include <cstring>

#include "record_descriptor_helpers.h"
#include "record_descriptor_offsets.h"

extern "C" uint32_t bb_render_get_record_descriptor_size(
    uint64_t *total, uint32_t *unit, const render::RecordDescriptorPrefix *record)
{
    using namespace render;
    constexpr uint32_t first_level = 0, initial_unit = 1, shift_mask = 31;
    constexpr uint32_t grouped_tag = 11, single_tag = 10, group_multiplier = 6;
    constexpr unsigned rounding_flag_byte = 1;
    constexpr uint8_t rounding_flag = 0x10, allow_third_component = 0x20;
    if (total == nullptr) return record_geometry_unsupported;
    if (unit == nullptr) return record_geometry_unsupported;
    if (record == nullptr) return record_geometry_unsupported;

    // The decoder owns initialization of its output fields. Keep its untouched
    // bytes when decoding fails; the caller deliberately ignores that status.
    RecordGeometryDescriptor original;
    initialize_record_level_descriptor(&original, record, first_level, 0);
    const uint64_t zero = 0;
    std::memcpy(total, &zero, sizeof(zero));
    std::memcpy(unit, &initial_unit, sizeof(initial_unit));
    RecordGeometryDescriptor working;
    std::memcpy(&working, &original, sizeof(working));

    uint64_t factor = uint32_t(record_descriptor_third_field(record) + 1);
    if (record_descriptor_high_field(record) == grouped_tag) factor *= group_multiplier;
    else if (record_descriptor_high_field(record) == single_tag) factor = 1;
    const uint32_t last_level = record_descriptor_level_count(record);
    if ((original.flags_24.bytes[rounding_flag_byte] & rounding_flag) != 0)
        factor = record_geometry_round_up_power_of_two(static_cast<uint32_t>(factor));
    const uint32_t level_count = last_level + 1;

    for (uint32_t level = first_level; level < level_count; ++level) {
        const uint32_t shift = level & shift_mask;
        const uint32_t first = original.first_08.get() >> shift;
        const uint32_t second = original.second_0c.get() >> shift;
        const uint32_t third = original.third_10.get() >> shift;
        working.first_08.set(first == 0 ? 1 : first);
        working.second_0c.set(second == 0 ? 1 : second);
        working.third_10.set(third == 0 ? 1 : third);
        uint32_t base = 0;
        if (level != first_level) base = record_descriptor_base_field(record) + 1;
        working.base_18.set(base);
        working.level_1c.set(level);
        if (working.third_10.get() != 1 && (working.flags_24.bytes[0] & allow_third_component) == 0)
            return record_geometry_unsupported;

        RecordGeometryResult geometry{};
        const uint32_t status = calculate_record_geometry(&geometry, &working);
        if (status != 0) return status;
        const uint32_t next_unit = geometry.value_1c.get();
        const uint64_t increment = read_geometry_total(geometry.total_10) * factor;
        uint64_t current_total;
        std::memcpy(&current_total, total, sizeof(current_total));
        current_total += increment;
        std::memcpy(total, &current_total, sizeof(current_total));
        // Reload only after the QWORD write: the two borrowed outputs, and the
        // record consulted by later accessors, can overlap one another.
        uint32_t current_unit;
        std::memcpy(&current_unit, unit, sizeof(current_unit));
        if (current_unit < next_unit) current_unit = next_unit;
        std::memcpy(unit, &current_unit, sizeof(current_unit));
        if (working.first_08.get() == 1 && working.second_0c.get() == 1 && working.third_10.get() == 1)
            return 0;
    }
    return 0;
}
