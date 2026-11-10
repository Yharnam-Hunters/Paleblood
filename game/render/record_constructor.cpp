// SPDX-License-Identifier: GPL-2.0-or-later
// Build the record prefix and optional packed auxiliary values consumed by the
// present-record builder. Failure can leave a cleared or partly packed record,
// and can follow an auxiliary publication. The full 64-bit return is packed data,
// so a zero return alone does not describe whether construction reached the tail.
#include <bit>
#include <cstring>

#include "../engine/integer.h"
#include "record_constructor.h"
#include "record_constructor_helpers.h"
#include "record_constructor_additional_calls.h"
#include "record_encoding.h"
#include "record_geometry_call.h"
#include "record_tuple.h"
#include "record_geometry_category_call.h"

namespace {
constexpr uint32_t low_byte_mask = 0xff, first_encoding = 1, encoding_count = 22;
constexpr uint32_t excluded_pair_bit = 2, excluded_encoding_pair = 15;
constexpr uint32_t component_alignment = 8, component_alignment_mask = 0xfffffff8;
constexpr uint32_t dword_shift_mask = 31, packed_word_shift = 32;
constexpr uint32_t multiplier_shift = 13, multiplier_mask = 0x00ffe000, preserve_multiplier = 0xff001800;
constexpr uint32_t encoding_flag_byte = 2;
constexpr size_t encoding_flag_byte_index = 2;
constexpr uint32_t first_selector_mask = 7, first_selector_shift = 12;
constexpr uint32_t second_selector_mask = 3, second_selector_shift = 15;
constexpr uint32_t preserve_control = 0xfffe0c00, classification_mask = 31, classification_shift = 5;
constexpr uint32_t first_component_shift = 3, first_component_mask = 0x7ff;
constexpr uint32_t repeated_component_shift = 20, repeated_component_mask = 0x7ff00000;
constexpr uint32_t preserve_component = 0x800ff800;
constexpr uint32_t area_shift = 6, area_mask = 0x003fffff, preserve_area = 0xffc00000;
constexpr uint32_t component_half_mask = 0xffff, component_half_shift = 16;
constexpr uint32_t excluded_auxiliary_category = 4, excluded_auxiliary_metadata = 128;
constexpr uint32_t auxiliary_quotient_shift = 7, auxiliary_quotient_mask = 0x3fff;
constexpr uint32_t preserve_auxiliary_quotient = 0xffffc000;
constexpr uint32_t preserve_first_auxiliary_flags = 0xfff7dfff, first_auxiliary_flag = 0x2000;
constexpr uint32_t second_auxiliary_category = 1, alternate_auxiliary_classification = 14;
constexpr uint32_t preserve_second_classification = 0xfffffc1f, second_auxiliary_flag_byte = 0x40;

uint64_t pack_words(uint32_t low, uint32_t high)
{
    return uint64_t(low) | (uint64_t(high) << packed_word_shift);
}
}  // namespace

extern "C" uint64_t bb_render_record_constructor(render::RecordConstructorPrefix *record,
                                                 uint32_t first, uint32_t second, uint32_t multiplier,
                                                 uint32_t encoding, uint32_t classification,
                                                 uint32_t first_selector, uint32_t second_selector,
                                                 uint64_t *first_auxiliary, uint64_t *second_auxiliary)
{
    // Clearing precedes admission and deliberately stops before the record tail.
    record->tail_28.set(0);
    render::clear_record_constructor_pair(record->auxiliary);
    render::clear_record_constructor_pair(record->dimensions);
    render::clear_record_constructor_pair(record->pair_10);
    render::clear_record_constructor_pair(record->sizing);
    render::clear_record_constructor_pair(record->pair_00);
    if (first_selector < second_selector) return 0;
    const uint32_t encoding_byte = encoding & low_byte_mask;
    if (encoding_byte - first_encoding >= encoding_count) return 0;
    if ((encoding_byte | excluded_pair_bit) == excluded_encoding_pair) return 0;
    if (render::extract_record_encoding_category(&encoding, nullptr) == 0) return 0;
    if (render::extract_record_encoding_components(&encoding, nullptr) == 0) return 0;

    render::RecordGeometryDescriptor descriptor{};
    descriptor.first_08.set(first_auxiliary != nullptr ? (first + component_alignment - 1) & component_alignment_mask : first);
    descriptor.second_0c.set(first_auxiliary != nullptr ? (second + component_alignment - 1) & component_alignment_mask : second);
    descriptor.third_10.set(1);
    descriptor.metadata_28.set(render::read_record_tuple_encoding_metadata(reinterpret_cast<const uint8_t *>(&encoding)));
    descriptor.range_2c = static_cast<uint8_t>(render::record_tuple_mapping_in_range(encoding));
    descriptor.count_14.set(1u << (second_selector & dword_shift_mask));
    descriptor.classification_00.set(classification);
    render::RecordGeometryResult geometry{};
    if (render::calculate_record_geometry(&geometry, &descriptor) != 0) return 0;
    const uint32_t converted = geometry.converted_30.get();

    const uint32_t packed_multiplier = ((multiplier << multiplier_shift) + multiplier_mask) & multiplier_mask;
    record->sizing.multiplier.set((record->sizing.multiplier.get() & preserve_multiplier) | packed_multiplier);
    render::set_record_constructor_encoding(record, encoding);
    record->pair_10.low.bytes[encoding_flag_byte_index] |= encoding_flag_byte;
    const uint32_t control = (record->pair_10.high.get() & preserve_control) |
                             ((first_selector & first_selector_mask) << first_selector_shift) |
                             ((second_selector & second_selector_mask) << second_selector_shift);
    const uint32_t geometry_first = geometry.first_00.get();
    const uint32_t geometry_second = geometry.second_04.get();
    const uint32_t component = (geometry_first >> first_component_shift) - 1;
    record->pair_00.high.set((record->pair_00.high.get() & preserve_component) |
                            (component & first_component_mask) |
                            ((component << repeated_component_shift) & repeated_component_mask));
    const uint32_t area = (geometry_first * geometry_second) >> area_shift;
    const uint32_t packed_area = (record->sizing.area.get() & preserve_area) | ((area + area_mask) & area_mask);
    const uint32_t selected_classification = converted & classification_mask;
    record->sizing.area.set(packed_area);
    record->dimensions.components.set((second << component_half_shift) | (first & component_half_mask));
    record->pair_10.high.set(control | selected_classification | (selected_classification << classification_shift));

    if (first_auxiliary != nullptr) {
        uint32_t category;
        // The native caller ignores status and consumes only the helper's output.
        render::get_record_geometry_category(&category, converted);
        if (category == excluded_auxiliary_category) return 0;
        if (render::read_record_tuple_encoding_metadata(reinterpret_cast<const uint8_t *>(&encoding)) == excluded_auxiliary_metadata)
            return 0;
        uint64_t total;
        uint32_t unit;
        if (render::record_constructor_optional_first(&total, &unit, nullptr, nullptr,
                                                      geometry.first_00.get(), geometry.second_04.get(), multiplier, 0) != 0)
            return 0;
        const uint64_t packed = pack_words(static_cast<uint32_t>(total), unit);
        std::memcpy(first_auxiliary, &packed, sizeof(packed));
        // Publication precedes this native division, including its zero-divisor fault.
        // Narrow the quotient before shifting; later updates can observe an aliased output.
        const uint32_t quotient = static_cast<uint32_t>(engine::divide_u64(total, uint64_t(multiplier)).quotient) >> auxiliary_quotient_shift;
        record->auxiliary.quantity.set((record->auxiliary.quantity.get() & preserve_auxiliary_quotient) |
                               ((quotient + auxiliary_quotient_mask) & auxiliary_quotient_mask));
        record->pair_10.low.set((record->pair_10.low.get() & preserve_first_auxiliary_flags) | first_auxiliary_flag);
    }

    if (second_auxiliary != nullptr && std::bit_cast<int32_t>(first_selector) > 0) {
        uint32_t category;
        render::get_record_geometry_category(&category, converted);
        const uint32_t selected = category == second_auxiliary_category ? converted : alternate_auxiliary_classification;
        record->pair_10.high.set((record->pair_10.high.get() & preserve_second_classification) |
                                ((selected & classification_mask) << classification_shift));
        if (first_auxiliary == nullptr) return 0;
        record->pair_10.low.bytes[1] |= second_auxiliary_flag_byte;
        const uint32_t packed_pair = render::record_constructor_pack_pair(first_selector, second_selector);
        uint64_t total;
        uint32_t unit, auxiliary_first, auxiliary_second;
        if (render::record_constructor_optional_second(&total, &unit, &auxiliary_first, &auxiliary_second,
                                                       geometry.first_00.get(), geometry.second_04.get(),
                                                       multiplier, selected, packed_pair) != 0)
            return 0;
        const uint64_t packed = pack_words(static_cast<uint32_t>(total), unit);
        std::memcpy(second_auxiliary, &packed, sizeof(packed));
        const uint32_t auxiliary_area = (auxiliary_second * auxiliary_first) >> area_shift;
        record->tail_28.set((record->tail_28.get() & preserve_area) | ((auxiliary_area + area_mask) & area_mask));
    }

    const uint64_t product = uint64_t(multiplier) * render::read_geometry_total(geometry.total_10);
    const uint32_t low = (product >> packed_word_shift) == 0 ? static_cast<uint32_t>(product) : 0;
    return pack_words(low, geometry.value_1c.get());
}
