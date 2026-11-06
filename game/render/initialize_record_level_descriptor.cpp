// SPDX-License-Identifier: GPL-2.0-or-later
// Decode one packed record level into geometry input. Only the flags word is
// cleared; errors retain prior field publications and the untouched tail bytes.
// Keep source getters interleaved with output writes for overlapping storage.
#include "record_descriptor_decoder.h"

#include <cstring>

extern "C" uint32_t bb_render_initialize_record_level_descriptor(
    render::RecordGeometryDescriptor *output, const render::RecordDescriptorPrefix *record,
    uint32_t level, uint32_t index)
{
    using namespace render;
    constexpr uint32_t unsupported = record_geometry_unsupported;
    constexpr uint32_t category_with_encoding = 2, triple_kind = 10, flagged_kind = 11;
    constexpr uint32_t preserve_first_flag = 0xfffffffd, preserve_kind_flags = 0xffffffcb;
    constexpr uint32_t preserve_rounding_flag = 0xffffefff, third_component_flag = 0x20;
    constexpr unsigned first_flag_shift = 1, second_flag_shift = 2;
    constexpr unsigned flagged_kind_shift = 4, triple_kind_shift = 5, rounding_flag_shift = 12;
    constexpr uint32_t native_shift_mask = 31, flagged_multiplier = 6;

    if (record == nullptr || record_descriptor_level_count(record) < level) return unsupported;
    const uint32_t kind = record_descriptor_high_field(record);
    output->flags_24.set(0);
    uint32_t category;
    const uint32_t category_status = get_record_geometry_category(
        &category, record_descriptor_classification(record));
    if (category_status != 0) return category_status;

    bool first_flag = false, second_flag = false;
    const bool needs_encoding_flags = category == category_with_encoding && kind != triple_kind;
    if (needs_encoding_flags) {
        uint32_t first_encoding = record_descriptor_encoding(record);
        first_flag = record_encoding_byte_flags(&first_encoding) != 0;
    }
    // This RMW also occurs when the first predicate was skipped.
    output->flags_24.set((output->flags_24.get() & preserve_first_flag)
        | (uint32_t(first_flag) << first_flag_shift));
    if (needs_encoding_flags) {
        uint32_t second_encoding = record_descriptor_encoding(record);
        second_flag = record_encoding_byte_is_one(&second_encoding) != 0;
    }
    output->flags_24.set((output->flags_24.get() & preserve_kind_flags)
        | (uint32_t(second_flag) << second_flag_shift)
        | (uint32_t(kind == flagged_kind) << flagged_kind_shift)
        | (uint32_t(kind == triple_kind) << triple_kind_shift));
    const uint32_t rounding_flag = record_descriptor_rounding_flag(record);
    output->flags_24.set((output->flags_24.get() & preserve_rounding_flag)
        | (rounding_flag << rounding_flag_shift));
    output->classification_00.set(record_descriptor_classification(record));
    output->encoding_04.set(0);
    uint32_t encoding = record_descriptor_encoding(record);
    const auto *encoding_byte = reinterpret_cast<const uint8_t *>(&encoding);
    output->metadata_28.set(read_record_tuple_encoding_metadata(encoding_byte));
    output->range_2c = record_tuple_mapping_in_range(encoding);

    uint32_t dimension = (record_descriptor_first_field(record) + 1) >> (level & native_shift_mask);
    output->first_08.set(dimension != 0 ? dimension : 1);
    dimension = (record_descriptor_second_field(record) + 1) >> (level & native_shift_mask);
    output->second_0c.set(dimension != 0 ? dimension : 1);
    dimension = 1;
    if (output->flags_24.get() & third_component_flag) {
        dimension = (record_descriptor_third_field(record) + 1) >> (level & native_shift_mask);
        if (dimension == 0) dimension = 1;
    }
    output->third_10.set(dimension);
    const uint32_t exponent = static_cast<uint8_t>(record_descriptor_count_exponent(record));
    output->count_14.set(uint32_t(1) << (exponent & native_shift_mask));
    output->base_18.set(level != 0 ? record_descriptor_base_field(record) + 1 : 0);
    output->level_1c.set(level);
    if (index != 0 && (output->flags_24.get() & third_component_flag)) return unsupported;

    uint32_t count_limit = record_descriptor_third_field(record) + 1;
    if (kind == flagged_kind) count_limit *= flagged_multiplier;
    else if (kind == triple_kind) count_limit = 1;
    if (record_descriptor_rounding_flag(record) != 0)
        count_limit = record_geometry_round_up_power_of_two(count_limit);
    if (count_limit <= index) return unsupported;
    output->index_20.set(index);
    RecordGeometryResult geometry{};
    const uint32_t geometry_status = calculate_record_geometry(&geometry, output);
    if (geometry_status != 0) return geometry_status;
    return convert_record_descriptor_classification(&output->classification_00,
        output->classification_00.get(), geometry.classification_18.get());
}
