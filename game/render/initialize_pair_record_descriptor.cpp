// SPDX-License-Identifier: GPL-2.0-or-later
// Construct the pair descriptor used by the render-record builder. Admission
// failures preserve the output; later errors leave the fields already published.
// The return packs two size values and can be zero after partial construction.
#include "record_descriptor_constructor.h"

extern "C" uint64_t bb_render_initialize_pair_record_descriptor(
    render::RecordDescriptorPrefix *output, uint32_t first, uint32_t second,
    uint32_t third, uint32_t count, uint32_t encoding, uint32_t classification,
    uint32_t level, uint8_t flag)
{
    using namespace render;
    constexpr unsigned tag_shift = 28, repeated_shift = 25, count_flag_shift = 12;
    constexpr unsigned byte_flag_shift = 4, first_width = 14, geometry_shift = 13;
    constexpr unsigned encoding_high_shift = 18, encoding_low_shift = 20;
    constexpr unsigned component_width = 3, count_shift = 16, classification_shift = 20;
    constexpr uint32_t native_shift_mask = 31, initial_tag = 2, repeated = 1;
    constexpr uint32_t preserve_tag = 0x0fffffff, preserve_pair = 0xf0000000;
    constexpr uint32_t component_mask = 7, first_mask = 0x3fff;
    constexpr uint32_t second_mask = 0x0fffc000, geometry_mask = 0x07ffe000;
    constexpr uint32_t preserve_geometry = 0xf8001fff, preserve_encoding = 0xc00fffff;
    constexpr uint32_t encoding_high_mask = 0x3c000000, encoding_low_mask = 0x3f;
    constexpr uint32_t preserve_components = 0xfffff000, preserve_third = 0xffffe000;
    constexpr uint32_t third_mask = 0x1fff, preserve_range = 0xfc000000;
    constexpr uint32_t range_mask = 0x03ffe000, preserve_count = 0xfff00fff;
    constexpr uint32_t count_mask = 0x000f0000, level_mask = 15;
    constexpr uint32_t final_encoding_mask = 0xfff000ff;
    constexpr uint32_t preserve_classification = 0xfe0fffff, classification_mask = 31;
    constexpr uint32_t flagged_tag = 11, single_tag = 9, multiple_tag = 13;
    constexpr uint32_t leveled_single_tag = 14, leveled_multiple_tag = 15;
    constexpr uint32_t flagged_multiplier = 6;
    constexpr unsigned packed_high_shift = 32;

    if (count == 0) return 0;
    const uint32_t largest = first > second ? first : second;
    if (count > record_descriptor_count_limit(largest)) return 0;
    if (first != second && flag == 1) return 0;
    if (count != 1 && level != 0) return 0;

    output->pair_18.clear();
    output->pair_10.clear();
    output->pair_08.clear();
    output->pair_00.clear();
    output->pair_00.high.set(0);
    const uint32_t more = count > 1;
    output->pair_08.high.set((more | initial_tag) << repeated_shift);

    RecordGeometryDescriptor descriptor{};
    descriptor.classification_00.set(classification);
    descriptor.first_08.set(first);
    descriptor.second_0c.set(second);
    descriptor.third_10.set(1);
    descriptor.flags_24.set((uint32_t(flag) << byte_flag_shift) | (more << count_flag_shift));
    const auto *encoding_byte = reinterpret_cast<const uint8_t *>(&encoding);
    descriptor.metadata_28.set(read_record_tuple_encoding_metadata(encoding_byte));
    descriptor.range_2c = record_tuple_mapping_in_range(encoding);
    descriptor.count_14.set(repeated << (level & native_shift_mask));
    RecordGeometryResult geometry{};
    if (calculate_record_geometry(&geometry, &descriptor) != 0) return 0;

    uint32_t tag;
    if (flag != 0) tag = flagged_tag;
    else if (static_cast<int32_t>(level) > 0)
        tag = third == 1 ? leveled_single_tag : leveled_multiple_tag;
    else tag = third == 1 ? single_tag : multiple_tag;
    const uint32_t tagged_word = (output->pair_08.high.get() & preserve_tag) | (tag << tag_shift);
    output->pair_08.high.set(tagged_word);
    output->pair_08.low.set((output->pair_08.low.get() & preserve_pair)
        | ((first + first_mask) & first_mask)
        | (((second << first_width) + second_mask) & second_mask));
    output->pair_10.low.set((output->pair_10.low.get() & preserve_geometry)
        | (((geometry.first_00.get() << geometry_shift) + geometry_mask) & geometry_mask));
    uint32_t component_encoding = encoding;
    output->pair_00.high.set((output->pair_00.high.get() & preserve_encoding)
        | ((encoding << encoding_high_shift) & encoding_high_mask)
        | ((encoding & encoding_low_mask) << encoding_low_shift));

    const uint32_t lane0 = read_record_descriptor_component(&component_encoding, 0) & component_mask;
    const uint32_t lane1 = read_record_descriptor_component(&component_encoding, 1) & component_mask;
    const uint32_t lane2 = read_record_descriptor_component(&component_encoding, 2) & component_mask;
    const uint32_t lane3 = read_record_descriptor_component(&component_encoding, 3) & component_mask;
    const uint32_t components = (tagged_word & preserve_components) | lane0
        | (lane1 << component_width) | (lane2 << (component_width * 2))
        | (lane3 << (component_width * 3));
    output->pair_08.high.set(components);
    output->pair_10.low.set((output->pair_10.low.get() & preserve_third)
        | ((third + third_mask) & third_mask));
    const uint32_t range = flag != 0 ? third * flagged_multiplier : third;
    output->pair_10.high.set((output->pair_10.high.get() & preserve_range)
        | (((range << geometry_shift) + range_mask) & range_mask));
    const uint32_t count_base = components & preserve_count;
    uint32_t count_word = count_base | (((count << count_shift) + count_mask) & count_mask);
    output->pair_08.high.set(count_word);
    output->pair_00.high.set(output->pair_00.high.get() & final_encoding_mask);
    if (static_cast<int32_t>(level) > 0) {
        // The level replaces the count field using the saved base word.
        count_word = count_base | ((level & level_mask) << count_shift);
        output->pair_08.high.set(count_word);
    }
    output->pair_08.high.set((count_word & preserve_classification)
        | ((geometry.converted_30.get() & classification_mask) << classification_shift));

    uint64_t total;
    uint32_t unit;
    if (get_record_descriptor_size(&total, &unit, output) != 0) return 0;
    const uint32_t low = total >> packed_high_shift ? 0 : static_cast<uint32_t>(total);
    return (uint64_t(unit) << packed_high_shift) | low;
}
