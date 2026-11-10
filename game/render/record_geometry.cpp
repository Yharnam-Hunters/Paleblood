// SPDX-License-Identifier: GPL-2.0-or-later
// Calculate dimensions and alignment for the record constructor. The result
// is published in stages, including on failure. Descriptor/result overlap makes
// the late descriptor reads and the live result-field reads significant.
#include <algorithm>

#include "record_geometry.h"
#include "record_geometry_helpers.h"
#include "record_tuple.h"

#include "tile_modes.h"

namespace {
using render::RecordGeometryDescriptor;
using render::RecordGeometryResult;

constexpr uint32_t unsupported = render::record_geometry_unsupported;
constexpr uint32_t invalid_calculation = 0x80d30001;
constexpr uint32_t allow_third_component = 0x20, require_area_multiple = 0x02;
constexpr uint32_t align_components = 0x10, round_components_byte = 0x10;
constexpr uint32_t dword_shift_mask = 31, byte_size_shift = 3, byte_rounding = 7;
constexpr uint32_t count_scale_shift = 3, subdivision_scale_shift = 3;
constexpr uint32_t bank_count_base = 2, minimum_bank_span = 4;
constexpr uint32_t multiple_count_begin = 2;
constexpr uint32_t range_component_scale = 4, range_increment = 3, range_shift = 2;
constexpr uint32_t metadata_limit = 128, metadata_range_shift = 4;
constexpr uint32_t classification_limit = 15, count_limit = 16;
constexpr uint32_t grouped_minimum = 2, large_group = 8;
constexpr uint32_t standard_alignment = 8, small_capacity = 64;
constexpr uint32_t calculation_unit = 256, maximum_product = 1024;
constexpr uint32_t scale_shift = 6, scaled_mask = 0x1ffffff8;
constexpr uint32_t two_component_mode = 2, three_component_mode = 3;
constexpr uint32_t third_fallback_classes = 0xe780, second_fallback_classes = 0x1870;

struct GeometryState {
    uint32_t first, second, third;
    uint32_t count, level, word, metadata;
    uint32_t classification, group, factor, mode;
    bool align, area_multiple;
};

uint32_t shift_left(uint32_t value, uint32_t shift)
{
    return value << (shift & dword_shift_mask);
}

uint32_t round_masked(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & (0u - alignment);
}

uint32_t reduce_range_classification(uint32_t classification)
{
    switch (classification) {
    case render::array_mode::tiled_1d_thick: return render::array_mode::tiled_1d_thin;
    case render::array_mode::tiled_2d_thick: case render::array_mode::tiled_2d_extra_thick: return render::array_mode::tiled_2d_thin;
    case render::array_mode::prt_thick: return render::array_mode::prt_thin;
    case render::array_mode::prt_2d_thick: return render::array_mode::prt_2d_thin;
    case render::array_mode::tiled_3d_thick: case render::array_mode::tiled_3d_extra_thick: return render::array_mode::tiled_3d_thin;
    case render::array_mode::prt_3d_thick: return render::array_mode::prt_3d_thin;
    default: return classification;
    }
}

uint32_t align_geometry(RecordGeometryResult *output, GeometryState &state)
{
    if (state.level != 0 && state.align)
        state.mode = two_component_mode | uint32_t(state.third > 1);
    const uint32_t mode = state.mode != 0 ? state.mode : three_component_mode;

    if (render::record_tuple_parameter_supported(output->first_alignment_20.get()) == 0)
        return invalid_calculation;
    const uint32_t first_alignment = output->first_alignment_20.get();
    if (mode >= two_component_mode) {
        if (render::record_tuple_parameter_supported(output->second_alignment_24.get()) == 0)
            return invalid_calculation;
        state.second = round_masked(state.second, output->second_alignment_24.get());
    }
    state.first = round_masked(state.first, first_alignment);
    if (mode == three_component_mode || state.group >= grouped_minimum) {
        if (state.align)
            state.third = render::record_geometry_round_up_power_of_two(state.third);
        if (state.group >= grouped_minimum) {
            if (render::record_tuple_parameter_supported(output->third_alignment_28.get()) == 0)
                return invalid_calculation;
            state.third = round_masked(state.third, output->third_alignment_28.get());
        }
    }
    return 0;
}

void publish_geometry(RecordGeometryResult *output, const GeometryState &state, uint64_t byte_count)
{
    output->first_00.set(state.first);
    output->second_04.set(state.second);
    output->third_08.set(state.third);
    output->total_10.set(byte_count * state.third);
    output->classification_18.set(state.classification);
}

uint32_t calculate_first_classes(RecordGeometryResult *output, GeometryState &state)
{
    const uint32_t rounded_bytes = (state.metadata + byte_rounding) >> byte_size_shift;
    if (state.classification == 0) {
        output->value_1c.set(state.metadata < standard_alignment + 1 ? 1 : rounded_bytes);
        output->first_alignment_20.set(1);
        output->second_alignment_24.set(1);
    } else {
        output->value_1c.set(calculation_unit);
        // Preserve the native wide division before publishing the first alignment.
        const uint64_t quotient = uint64_t(small_capacity) / rounded_bytes;
        output->first_alignment_20.set(static_cast<uint32_t>(std::max<uint64_t>(quotient, standard_alignment)));
        output->second_alignment_24.set(1);
    }
    output->third_alignment_28.set(state.group);
    const uint32_t status = align_geometry(output, state);
    if (status != 0) return status;

    uint64_t byte_count;
    if (state.classification == 1) {
        const uint32_t divisor = std::max(calculation_unit / rounded_bytes, small_capacity);
        if (state.count != 1) return unsupported;
        if ((uint64_t(state.first) * state.second) % divisor != 0) {
            const uint32_t step = output->first_alignment_20.get();
            do {
                state.first += step;
            } while ((uint64_t(state.first) * state.second) % divisor != 0);
        }
        uint32_t multiple = state.first, rows = 0, remainder;
        do {
            remainder = multiple % divisor;
            ++rows;
            multiple += state.first;
        } while (remainder != 0);
        output->second_alignment_24.set(rows);
        byte_count = (uint64_t(state.first) * state.second * state.metadata + byte_rounding) >> byte_size_shift;
    } else {
        byte_count = (uint64_t(state.metadata) * state.count * state.first * state.second + byte_rounding) >> byte_size_shift;
    }
    publish_geometry(output, state, byte_count);
    return 0;
}

uint32_t calculate_fallback_classes(RecordGeometryResult *output, GeometryState &state)
{
    // Direct class 2/3 admission is outside this helper. Complex-path fallbacks
    // enter here without the direct path's count==1 requirement.
    state.group = render::record_tuple_classification_group(state.classification);
    output->value_1c.set(calculation_unit);
    output->first_alignment_20.set(standard_alignment);
    output->second_alignment_24.set(standard_alignment);
    output->third_alignment_28.set(state.group);
    const uint32_t status = align_geometry(output, state);
    if (status != 0) return status;

    const uint32_t starting_first = state.first;
    uint32_t numerator = state.count * state.metadata * state.second * state.first + byte_rounding;
    uint32_t bytes = numerator >> byte_size_shift;
    const uint32_t unit = output->value_1c.get();
    if ((uint64_t(bytes) * state.group) % unit != 0) {
        const uint32_t step = output->first_alignment_20.get();
        const uint32_t row_increment = state.metadata * state.second * state.count * step;
        do {
            state.first += step;
            numerator += row_increment;
            bytes = numerator >> byte_size_shift;
        } while ((uint64_t(bytes) * state.group) % unit != 0);
    }
    uint64_t byte_count = bytes;
    if (state.area_multiple) {
        if ((state.first * state.second) % unit != 0) {
            const uint32_t step = output->first_alignment_20.get();
            do {
                state.first += step;
            } while ((state.first * state.second) % unit != 0);
        }
        // Compare against the component saved before BOTH modulo loops. Only
        // this recomputation widens after the area product has wrapped to 32 bits.
        if (state.first != starting_first) {
            const uint32_t area = state.first * state.second;
            byte_count = (uint64_t(area) * state.metadata + byte_rounding) >> byte_size_shift;
        }
    }
    // The group participates in the modulo test, not in this byte count.
    publish_geometry(output, state, byte_count);
    return 0;
}

uint32_t prepare_complex_geometry(RecordGeometryResult *output, GeometryState &state, bool &fallback)
{
    uint32_t previous_classification = state.classification;
    state.group = render::record_tuple_classification_group(state.classification);
    uint32_t first_exponent;
    uint32_t status = render::get_record_geometry_first_exponent(&first_exponent, state.word);
    if (status != 0) return status;
    const uint32_t count_scaled = state.count << count_scale_shift;
    const uint32_t count_capacity = state.count << scale_shift;

    for (;;) {
        uint32_t second_exponent, first_field, second_field, third_field, fourth_field, category;
        status = render::get_record_geometry_second_exponent(&second_exponent, state.word);
        if (status != 0) return status;
        status = render::get_record_geometry_parameters(state.word, state.metadata, state.count,
                                                        &first_field, &second_field, &third_field, &fourth_field);
        if (status != 0) return status;
        status = render::get_record_geometry_category(&category, state.word);
        if (status != 0) return status;

        const uint32_t doubled_fourth = shift_left(bank_count_base, fourth_field);
        uint32_t third_scale = shift_left(1, third_field);
        uint32_t first_scale = shift_left(1, first_field);
        uint32_t second_scale = shift_left(1, second_field);
        const uint32_t grouped_metadata = state.group * state.metadata;
        const uint32_t capacity = category == render::micro_tile_mode::depth
            ? shift_left(small_capacity, first_exponent)
            : std::clamp(shift_left((grouped_metadata << subdivision_scale_shift) & scaled_mask, second_exponent),
                         calculation_unit, maximum_product);
        const uint32_t fourth_scaled = doubled_fourth << subdivision_scale_shift;
        if (doubled_fourth < third_scale || fourth_scaled < minimum_bank_span) return invalid_calculation;
        const uint32_t scale = std::min((grouped_metadata * count_scaled) & scaled_mask, capacity);
        const uint32_t first_product = shift_left(scale, first_field);
        const uint32_t ratio = std::max(calculation_unit / first_product, 1u);
        // A wrapped zero divisor must reach the native division; there is no
        // substitute status or protective divisor clamp before it.
        if (render::record_tuple_parameter_supported(ratio) == 0) return invalid_calculation;
        if (state.count == 1) {
            const uint32_t third_ratio = std::max(calculation_unit / shift_left(scale << subdivision_scale_shift, first_field), 1u);
            if (render::record_tuple_parameter_supported(third_ratio) == 0) return invalid_calculation;
            third_scale = round_masked(third_scale, third_ratio);
        }
        second_scale = round_masked(second_scale, ratio);
        // Admitted parameter-table entries have first_field == 0. For second_field
        // 1/2/3, their resolver choices cap scale at 256/128/64; second_field 0
        // allows at most 1024. Rounding second_scale to the 256-unit ratio cannot
        // make their product exceed 1024. The reduction therefore stays unreachable
        // with the shipped read-only tables, but remains part of the table-driven logic.
        if (first_product * second_scale > maximum_product) {
            bool excessive = true;
            uint32_t chosen_first = first_scale;
            if (first_field != 0) {
                const uint32_t base_product = second_scale * scale;
                do {
                    first_scale >>= 1;
                    chosen_first = 1;
                    if (first_scale == 0) break;
                    excessive = base_product * first_scale > maximum_product;
                    chosen_first = first_scale;
                } while (excessive);
            }
            const uint32_t reduced_product = chosen_first * scale;
            const uint32_t reduced_ratio = std::max(calculation_unit / reduced_product, 1u);
            if (render::record_tuple_parameter_supported(reduced_ratio) == 0) return invalid_calculation;
            if (second_scale % reduced_ratio != 0) return invalid_calculation;
            if (state.count == 1) {
                const uint32_t third_ratio = std::max(calculation_unit / (reduced_product << subdivision_scale_shift), 1u);
                if (render::record_tuple_parameter_supported(third_ratio) == 0) return invalid_calculation;
                third_scale = round_masked(third_scale, third_ratio);
            }
            if (excessive && (!state.area_multiple || state.metadata < small_capacity)) {
                while (second_scale > reduced_ratio) {
                    second_scale >>= 1;
                    if (second_scale < reduced_ratio) {
                        second_scale = reduced_ratio;
                        break;
                    }
                    if (second_scale * reduced_product <= maximum_product) break;
                }
            }
            first_scale = chosen_first;
        }

        output->first_alignment_20.set((third_scale * first_scale) << scale_shift);
        output->second_alignment_24.set((fourth_scaled * second_scale) / third_scale);
        output->third_alignment_28.set(state.group);
        output->value_1c.set(fourth_scaled * scale * first_scale * second_scale);
        const uint32_t scaled_first = scale * first_scale;
        if (scaled_first * second_scale < calculation_unit ||
            scaled_first * (third_scale << subdivision_scale_shift) < calculation_unit) return invalid_calculation;

        bool restart;
        if (state.level != 0) {
            const uint32_t rounded_metadata = render::record_geometry_round_up_power_of_two(state.metadata);
            const uint32_t threshold = std::min((count_capacity * state.group * rounded_metadata) >> byte_size_shift, capacity);
            if (state.classification <= classification_limit &&
                ((third_fallback_classes >> state.classification) & 1) != 0) {
                if (state.first < output->first_alignment_20.get() || state.second < output->second_alignment_24.get()) {
                    state.classification = render::array_mode::tiled_1d_thick;
                    fallback = true;
                    return 0;
                }
            } else if (state.classification <= classification_limit &&
                       ((second_fallback_classes >> state.classification) & 1) != 0) {
                if (state.first < output->first_alignment_20.get() || state.second < output->second_alignment_24.get() ||
                    (third_scale << subdivision_scale_shift) * first_scale * threshold < calculation_unit ||
                    second_scale * first_scale * threshold < calculation_unit) {
                    state.classification = render::array_mode::tiled_1d_thin;
                    fallback = true;
                    return 0;
                }
            }
            const uint32_t current_group = render::record_tuple_classification_group(state.classification);
            restart = current_group != state.group || state.classification != previous_classification;
        } else {
            restart = state.classification != previous_classification;
        }
        if (!restart) return 0;
        state.group = render::record_tuple_classification_group(state.classification);
        status = render::get_record_geometry_first_exponent(&first_exponent, state.word);
        previous_classification = state.classification;
        if (status != 0) return status;
        // Only the second exponent and later calculations repeat on restart.
    }
}

uint32_t finish_geometry(RecordGeometryResult *output, const RecordGeometryDescriptor *descriptor,
                         const GeometryState &state)
{
    const uint32_t scaled_first = state.first * state.factor;
    output->first_00.set(scaled_first);
    output->second_04.set(state.second * state.factor);
    output->first_alignment_20.set(output->first_alignment_20.get() * state.factor);
    output->second_alignment_24.set(output->second_alignment_24.get() * state.factor);
    uint32_t classification = state.classification;
    // These reads deliberately happen after earlier output writes. In particular,
    // the descriptor's second flag byte can alias a freshly published result.
    if ((descriptor->flags_24.bytes[1] & round_components_byte) != 0) {
        output->first_00.set(render::record_geometry_round_up_power_of_two(scaled_first));
        output->second_04.set(render::record_geometry_round_up_power_of_two(output->second_04.get()));
        output->third_08.set(render::record_geometry_round_up_power_of_two(output->third_08.get()));
        classification = output->classification_18.get();
    }
    const uint32_t word = descriptor->classification_00.get();
    uint32_t final_classification;
    uint32_t status = render::get_record_geometry_classification(&final_classification, word);
    if (status != 0) return status;
    if (final_classification == classification) {
        output->converted_30.set(word);
        return 0;
    }
    uint32_t category;
    status = render::get_record_geometry_category(&category, word);
    if (status != 0) return status;
    uint32_t converted;
    switch (category) {
    case render::micro_tile_mode::display:
        if (classification != render::array_mode::tiled_1d_thin) return unsupported;
        converted = render::tile_mode_index::display_1d_thin;
        break;
    case render::micro_tile_mode::thin:
        if (classification != render::array_mode::tiled_1d_thin) return unsupported;
        converted = render::tile_mode_index::thin_1d;
        break;
    case render::micro_tile_mode::depth:
        converted = render::tile_mode_index::depth_1d_thin;
        break;
    case render::micro_tile_mode::thick:
        switch (classification) {
        case render::array_mode::tiled_1d_thin: converted = render::tile_mode_index::thin_1d; break;
        case render::array_mode::tiled_1d_thick: converted = render::tile_mode_index::thick_1d; break;
        case render::array_mode::tiled_2d_thin: converted = render::tile_mode_index::thin_2d; break;
        case render::array_mode::prt_thin: converted = render::tile_mode_index::prt_thin; break;
        case render::array_mode::prt_2d_thin: converted = render::tile_mode_index::prt_2d_thin; break;
        case render::array_mode::tiled_2d_thick: converted = render::tile_mode_index::thick_2d; break;
        case render::array_mode::prt_3d_thin: converted = render::tile_mode_index::prt_3d_thin; break;
        case render::array_mode::tiled_3d_thin: converted = render::tile_mode_index::thin_3d; break;
        case render::array_mode::tiled_3d_thick: converted = render::tile_mode_index::thick_3d; break;
        default: return unsupported;
        }
        break;
    default: return unsupported;
    }
    output->converted_30.set(converted);
    return 0;
}
}  // namespace

extern "C" uint32_t bb_render_record_geometry(RecordGeometryResult *output, const RecordGeometryDescriptor *descriptor)
{
    if (output == nullptr || descriptor == nullptr) return unsupported;
    if ((descriptor->flags_24.bytes[0] & allow_third_component) == 0 && descriptor->third_10.get() != 1)
        return unsupported;
    if (descriptor->level_1c.get() != 0) {
        if (descriptor->base_18.get() < descriptor->first_08.get()) return unsupported;
    } else if (descriptor->base_18.get() != 0) {
        return unsupported;
    }
    uint32_t metadata;
    bool range;
    if (descriptor->encoding_04.get() != 0) {
        metadata = render::read_record_tuple_encoding_metadata(descriptor->encoding_04.bytes);
        // The original re-reads encoding after the metadata helper. That helper
        // only reads it, so the range_2c fallback below is unreachable for ordinary
        // inputs; preserve the re-read rather than imply that it can change here.
        range = descriptor->encoding_04.get() != 0
            ? render::record_tuple_mapping_in_range(descriptor->encoding_04.bytes[0])
            : descriptor->range_2c != 0;
    } else {
        metadata = descriptor->metadata_28.get();
        range = descriptor->range_2c != 0;
    }
    if (render::record_tuple_parameter_supported(metadata) == 0) return unsupported;
    if (metadata == 0 || metadata > metadata_limit) return unsupported;

    GeometryState state;
    state.word = descriptor->classification_00.get();
    state.third = descriptor->third_10.get();
    state.first = descriptor->first_08.get();
    state.second = descriptor->second_0c.get();
    state.count = descriptor->count_14.get();
    const uint32_t base = descriptor->base_18.get();
    state.level = descriptor->level_1c.get();
    const uint32_t flags = descriptor->flags_24.get();
    uint32_t category;
    uint32_t status = render::get_record_geometry_category(&category, state.word);
    if (status != 0) return status;
    if (state.count >= multiple_count_begin && state.level != 0) return unsupported;
    state.area_multiple = (flags & require_area_multiple) != 0;
    if (state.area_multiple && category != render::micro_tile_mode::depth) return unsupported;
    if (state.level == 0 && range) {
        state.first = round_masked(state.first, range_component_scale);
        state.second = round_masked(state.second, range_component_scale);
    }
    if (state.level != 0)
        state.first = std::max(base >> (state.level & dword_shift_mask), 1u);
    // This first publication precedes the optional component-rounding calls.
    output->metadata_2c.set(metadata);
    state.metadata = range ? metadata << metadata_range_shift : metadata;
    const uint32_t increment = range ? range_increment : 0;
    const uint32_t reduction = range ? range_shift : 0;
    state.first = std::max((state.first + increment) >> reduction, 1u);
    state.second = std::max((state.second + increment) >> reduction, 1u);
    if ((descriptor->flags_24.bytes[1] & round_components_byte) != 0) {
        state.first = render::record_geometry_round_up_power_of_two(state.first);
        state.second = render::record_geometry_round_up_power_of_two(state.second);
        state.third = render::record_geometry_round_up_power_of_two(state.third);
    }
    status = render::get_record_geometry_classification(&state.classification, state.word);
    if (status != 0) return status;
    uint32_t initial_group = render::record_tuple_classification_group(state.classification);
    if (range && render::record_tuple_classification_is_special(state.classification)) return unsupported;
    state.factor = range ? range_component_scale : 1;
    if (initial_group >= grouped_minimum) {
        if (range) {
            const uint32_t reduced = reduce_range_classification(state.classification);
            if (reduced != state.classification) initial_group = 1;
            state.classification = reduced;
        }
        if (state.metadata > small_capacity && initial_group == large_group) return unsupported;
    }
    if (state.count - 1 >= count_limit) return unsupported;
    state.classification = render::record_tuple_transform_classification(state.classification, state.metadata);
    state.group = render::record_tuple_classification_group(state.classification);
    if (state.classification > classification_limit) return unsupported;
    state.align = (flags & align_components) != 0;
    state.mode = state.align && state.level == 0 ? two_component_mode : 0;

    if (state.classification < render::array_mode::tiled_1d_thin) {
        status = calculate_first_classes(output, state);
    } else if (state.classification < render::array_mode::tiled_2d_thin) {
        if (state.count != 1) return unsupported;
        status = calculate_fallback_classes(output, state);
    } else {
        bool fallback = false;
        status = prepare_complex_geometry(output, state, fallback);
        if (status != 0) return status;
        if (fallback) {
            status = calculate_fallback_classes(output, state);
        } else {
            status = align_geometry(output, state);
            if (status != 0) return status;
            const uint32_t rounded_metadata = render::record_geometry_round_up_power_of_two(state.metadata);
            const uint64_t byte_count = (uint64_t(state.first) * state.count * state.second * rounded_metadata + byte_rounding) >> byte_size_shift;
            publish_geometry(output, state, byte_count);
        }
    }
    if (status != 0) return status;
    return finish_geometry(output, descriptor, state);
}
