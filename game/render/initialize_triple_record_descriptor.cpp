// SPDX-License-Identifier: GPL-2.0-or-later
// Construct the triple descriptor used by the render-record builder. Delegate
// failures retain the clear and fields already written. The return is packed
// size data, with a zero low half when the total exceeds a DWORD.
#include "record_descriptor_constructor.h"

extern "C" uint64_t bb_render_initialize_triple_record_descriptor(
    render::RecordDescriptorPrefix *output, uint32_t first, uint32_t second,
    uint32_t third, uint32_t count, uint32_t encoding, uint32_t classification)
{
    using namespace render;
    constexpr unsigned tag_shift = 28, repeated_shift = 25, count_flag_shift = 12;
    constexpr unsigned first_width = 14, geometry_shift = 13;
    constexpr unsigned encoding_high_shift = 18, encoding_low_shift = 20;
    constexpr unsigned component_width = 3, count_shift = 16, classification_shift = 20;
    constexpr uint32_t initial_tag = 0x04000000, triple_selector = 8, final_tag = 10;
    constexpr uint32_t preserve_count_flag = 0xffffefff, preserve_repeated = 0xfdffffff;
    constexpr uint32_t preserve_tag = 0x0fffffff, preserve_pair = 0xf0000000;
    constexpr uint32_t component_mask = 7, first_mask = 0x3fff;
    constexpr uint32_t second_mask = 0x0fffc000, geometry_mask = 0x07ffe000;
    constexpr uint32_t preserve_geometry = 0xf8001fff, preserve_encoding = 0xc00fffff;
    constexpr uint32_t encoding_high_mask = 0x3c000000, encoding_low_mask = 0x3f;
    constexpr uint32_t preserve_components = 0xae000000, preserve_third = 0xffffe000;
    constexpr uint32_t third_mask = 0x1fff, preserve_range = 0xfc000000;
    constexpr uint32_t count_mask = 0x000f0000, final_encoding_mask = 0xfff000ff;
    constexpr uint32_t classification_mask = 31;
    constexpr unsigned packed_high_shift = 32;

    if (count == 0) return 0;
    uint32_t largest = first > second ? first : second;
    if (third > largest) largest = third;
    if (count > record_descriptor_count_limit(largest)) return 0;

    output->pair_18.clear();
    output->pair_10.clear();
    output->pair_08.clear();
    output->pair_00.clear();
    output->pair_08.high.set(initial_tag);
    output->pair_00.high.set(0);
    const uint32_t more = count > 1;
    uint32_t flags;
    if (initialize_record_geometry_flags(&flags, triple_selector, more) != 0) return 0;
    flags = (flags & preserve_count_flag) | (more << count_flag_shift);
    output->pair_08.high.set((output->pair_08.high.get() & preserve_repeated)
        | (more << repeated_shift));

    RecordGeometryDescriptor descriptor{};
    descriptor.classification_00.set(classification);
    descriptor.first_08.set(first);
    descriptor.second_0c.set(second);
    descriptor.third_10.set(third);
    descriptor.count_14.set(1);
    descriptor.flags_24.set(flags);
    const auto *encoding_byte = reinterpret_cast<const uint8_t *>(&encoding);
    descriptor.metadata_28.set(read_record_tuple_encoding_metadata(encoding_byte));
    descriptor.range_2c = record_tuple_mapping_in_range(encoding);
    RecordGeometryResult geometry{};
    if (calculate_record_geometry(&geometry, &descriptor) != 0) return 0;

    const uint32_t tagged_word = (output->pair_08.high.get() & preserve_tag) | (final_tag << tag_shift);
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
    output->pair_10.low.set((output->pair_10.low.get() & preserve_third)
        | ((third + third_mask) & third_mask));
    output->pair_10.high.set(output->pair_10.high.get() & preserve_range);
    const uint32_t count_bits = ((count << count_shift) + count_mask) & count_mask;
    output->pair_00.high.set(output->pair_00.high.get() & final_encoding_mask);
    output->pair_08.high.set(components | count_bits
        | ((geometry.converted_30.get() & classification_mask) << classification_shift));

    uint64_t total;
    uint32_t unit;
    if (get_record_descriptor_size(&total, &unit, output) != 0) return 0;
    const uint32_t low = total >> packed_high_shift ? 0 : static_cast<uint32_t>(total);
    return (uint64_t(unit) << packed_high_shift) | low;
}
