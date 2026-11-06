// SPDX-License-Identifier: GPL-2.0-or-later
// Preserve ordered packed-word publications and repeated exact-width reads.
// Input/output overlap can change later reads; no source snapshot is taken.
#include "constructor_descriptor_conversion.h"

namespace {
constexpr uint32_t initial_word0c = 0x04000000;
constexpr uint8_t word04_byte_keep = 0xc0;
constexpr uint32_t low14_mask = 0x3fff;
constexpr uint32_t word08_keep_high18 = 0xffffc000;
constexpr uint32_t word08_keep_outer = 0xf0003fff;
constexpr uint32_t word08_middle_mask = 0x0fffc000;
constexpr uint32_t word08_middle_shift = 14;
constexpr uint32_t high_nibble_keep = 0x0fffffff;

constexpr uint32_t word10_keep_outer = 0xf8001fff;
constexpr uint32_t word10_high_mask = 0x07ff0000;
constexpr uint32_t word10_high_shift = 16;
constexpr uint32_t word10_fixed_bits = 0x0000e000;
constexpr uint32_t word0c_clear_middle = 0xfff00fff;
constexpr uint32_t word04_clear_middle = 0xfff000ff;
constexpr uint32_t word04_keep_outer = 0xc00fffff;
constexpr uint32_t word04_upper_group_mask = 0x3c000000;
constexpr uint32_t word04_upper_group_shift = 18;
constexpr uint32_t word04_lower_group_mask = 0x3f;
constexpr uint32_t word04_lower_group_shift = 20;
constexpr uint32_t component_mask = 7;
constexpr uint32_t second_component_shift = 3;
constexpr uint32_t third_component_shift = 6;
constexpr uint32_t fourth_component_shift = 9;
constexpr uint32_t word0c_keep_high20 = 0xfffff000;
constexpr uint32_t word0c_keep_outer = 0xfe0fffff;
constexpr uint32_t word0c_middle_mask = 0x1f;
constexpr uint32_t word0c_middle_shift = 20;
constexpr uint32_t low13_mask = 0x1fff;
constexpr uint32_t word10_keep_high19 = 0xffffe000;
constexpr uint32_t word14_keep_high6 = 0xfc000000;
constexpr uint32_t word14_middle_mask = 0x03ffe000;
constexpr uint32_t word14_middle_shift = 13;
constexpr uint64_t divide_six_multiplier = 0xaaaaaaab;
constexpr uint32_t divide_six_shift = 34;
constexpr uint32_t word0c_type_special = 0xb0000000;
constexpr uint32_t word0c_type_positive = 0xf0000000;
constexpr uint32_t word0c_type_positive_empty = 0xe0000000;
constexpr uint32_t word0c_type_first_present = 0xd0000000;
constexpr uint32_t word0c_type_first_empty = 0x90000000;
constexpr uint32_t word0c_type_first_absent = 0xc0000000;
constexpr uint32_t word0c_type_both_empty = 0x80000000;
constexpr uint32_t final_nibble_mask = 0xf;
constexpr uint32_t final_nibble_shift = 16;
constexpr uint32_t word0c_keep_without_nibble = 0xfff0ffff;

void replace_high_nibble(render::Field<uint32_t> &field, uint32_t value)
{
    const uint32_t previous = field.get();
    field.set((previous & high_nibble_keep) | value);
}
}  // namespace

extern "C" void bb_render_convert_constructor_record(
    render::ConstructorDescriptorOutput *output,
    const render::ConstructorRecordSource *source, uint32_t flag)
{
    using namespace render;
    Field<uint32_t> mapping_word;

    output->words_18_1c.clear();
    output->words_10_14.clear();
    output->words_08_0c.clear();
    output->words_00_04.clear();
    output->words_08_0c.high.set(initial_word0c);
    output->words_00_04.high.set(0);

    const uint32_t word00 = constructor_conversion_calls::source_for_word00(source);
    output->words_00_04.low.set(word00);
    // Only the low BYTE is read and written here. Other bits stay live.
    uint8_t &word04_byte = output->words_00_04.high.bytes[0];
    word04_byte &= word04_byte_keep;

    const uint32_t low08 = constructor_conversion_calls::source_for_word08_low(source);
    const uint32_t adjusted_low08 = low08 + low14_mask;
    const uint32_t previous08_high = output->words_08_0c.low.get() & word08_keep_high18;
    output->words_08_0c.low.set(previous08_high | (adjusted_low08 & low14_mask));

    const uint32_t high08 = constructor_conversion_calls::source_for_word08_high(source);
    const uint32_t previous08_outer = output->words_08_0c.low.get() & word08_keep_outer;
    const uint32_t adjusted_high08 = ((high08 << word08_middle_shift) + word08_middle_mask) &
                                      word08_middle_mask;
    output->words_08_0c.low.set(previous08_outer | adjusted_high08);
    replace_high_nibble(output->words_08_0c.high, word0c_type_first_empty);

    const uint32_t high10 = constructor_conversion_calls::source_for_word10_high(source);
    const uint32_t previous10_outer = output->words_10_14.low.get() & word10_keep_outer;
    const uint32_t shifted_high10 = (high10 << word10_high_shift) & word10_high_mask;
    output->words_10_14.low.set(previous10_outer | shifted_high10 | word10_fixed_bits);
    output->words_08_0c.high.set(output->words_08_0c.high.get() & word0c_clear_middle);
    output->words_00_04.high.set(output->words_00_04.high.get() & word04_clear_middle);

    const uint32_t mapping = static_cast<uint32_t>(
        constructor_conversion_calls::source_mapping_word(source));
    mapping_word.set(mapping);
    const uint32_t previous04_outer = output->words_00_04.high.get() & word04_keep_outer;
    const uint32_t upper_group = (mapping << word04_upper_group_shift) & word04_upper_group_mask;
    const uint32_t lower_group = (mapping & word04_lower_group_mask) << word04_lower_group_shift;
    output->words_00_04.high.set(previous04_outer | upper_group | lower_group);

    // The high20 snapshot precedes every component call. All four originals
    // receive the same local storage, not four copied mapping words.
    const uint32_t previous0c_high = output->words_08_0c.high.get() & word0c_keep_high20;
    const auto *mapping_address = reinterpret_cast<const uint32_t *>(mapping_word.bytes);
    const uint32_t first_component = read_record_descriptor_component(
        mapping_address, 0);
    const uint32_t first_group = previous0c_high | (first_component & component_mask);
    const uint32_t second_component = read_record_descriptor_component(
        mapping_address, 1);
    const uint32_t second_group = first_group |
                                  ((second_component << second_component_shift) &
                                   (component_mask << second_component_shift));
    const uint32_t third_component = read_record_descriptor_component(
        mapping_address, 2);
    const uint32_t third_group = second_group |
                                 ((third_component & component_mask) << third_component_shift);
    const uint32_t fourth_component = read_record_descriptor_component(
        mapping_address, 3);
    output->words_08_0c.high.set(third_group |
        ((fourth_component & component_mask) << fourth_component_shift));

    const uint32_t middle0c = constructor_conversion_calls::source_for_word0c_middle(source);
    const uint32_t previous0c_outer = output->words_08_0c.high.get() & word0c_keep_outer;
    output->words_08_0c.high.set(previous0c_outer |
                                ((middle0c & word0c_middle_mask) << word0c_middle_shift));

    const uint32_t low10 = constructor_conversion_calls::source_for_word10_low(source);
    const uint32_t previous10_high = output->words_10_14.low.get() & word10_keep_high19;
    output->words_10_14.low.set(previous10_high | (low10 & low13_mask));

    const uint32_t low14 = constructor_conversion_calls::source_for_word14_low(source);
    // This is a distinct source_for_word10_low call after low14 was cached.
    const uint32_t second_low10 = constructor_conversion_calls::source_for_word10_low(source);
    const uint32_t previous14_high = output->words_10_14.high.get() & word14_keep_high6;
    const uint32_t middle14 = (second_low10 << word14_middle_shift) & word14_middle_mask;
    output->words_10_14.high.set(previous14_high | (low14 & low13_mask) | middle14);

    // The flag DWORD is read only if this cached middle field is nonzero.
    // Only a flag whose low byte is exactly 1 selects the special descriptor type.
    if (middle14 != 0 && uint8_t(flag) == 1) {
        const uint32_t current10 = output->words_10_14.low.get();
        const uint32_t count = (current10 & low13_mask) + 1;
        const uint32_t quotient = uint32_t((uint64_t(count) * divide_six_multiplier) >>
                                           divide_six_shift);
        const uint32_t adjusted = (quotient + low13_mask) & low13_mask;
        output->words_10_14.low.set((current10 & word10_keep_high19) | adjusted);
        replace_high_nibble(output->words_08_0c.high, word0c_type_special);
    } else {
        const uint32_t type_selector = constructor_conversion_calls::source_type_selector(source);
        uint32_t high_nibble;
        if (type_selector != 0) {
            const uint32_t current14 = output->words_10_14.high.get();
            high_nibble = word0c_type_positive;
            if ((current14 & low13_mask) == 0 && (current14 & word14_middle_mask) == 0)
                high_nibble = word0c_type_positive_empty;
        } else {
            const uint32_t current14 = output->words_10_14.high.get();
            const uint32_t first_field = current14 & low13_mask;
            // A populated middle field still prevents the empty type when the low field is zero.
            const uint32_t word08_middle = output->words_08_0c.low.get() & word08_middle_mask;
            if (word08_middle != 0) {
                high_nibble = word0c_type_first_present;
                if (first_field == 0 && (current14 & word14_middle_mask) == 0)
                    high_nibble = word0c_type_first_empty;
            } else {
                high_nibble = word0c_type_first_absent;
                if (first_field == 0 && (current14 & word14_middle_mask) == 0)
                    high_nibble = word0c_type_both_empty;
            }
        }
        replace_high_nibble(output->words_08_0c.high, high_nibble);
    }

    // Fetch the selector after descriptor updates, including when type selection
    // did not need it. Overlapping output can change this later value.
    const uint32_t final_parameter = constructor_conversion_calls::source_type_selector(source);
    if (final_parameter != 0) {
        const uint32_t previous = output->words_08_0c.high.get() & word0c_keep_without_nibble;
        output->words_08_0c.high.set(previous |
            ((final_parameter & final_nibble_mask) << final_nibble_shift));
    }
}
