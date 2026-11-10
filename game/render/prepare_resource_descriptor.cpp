// SPDX-License-Identifier: GPL-2.0-or-later
// Build the ordinary/special descriptor and accumulate its ordered requirements.
// Original helpers own their effects; partial descriptor writes survive errors.
#include "resource_descriptor_builder.h"

namespace {
constexpr uint32_t auxiliary_group_flag = 0x20;
constexpr uint32_t auxiliary_descriptor_flag = 0x40;
constexpr uint32_t first_auxiliary_flag = 0x200;
constexpr uint32_t special_parameter_flag = 0x400;
constexpr uint32_t reduced_quantity_flag = 4;
constexpr uint32_t first_auxiliary_shift = 9;
constexpr uint32_t secondary_byte_shift = 8, secondary_byte_mask = 1;
constexpr uint32_t second_descriptor_parameter = 0x20401;
constexpr uint32_t mapped_auxiliary_failure = 5;
constexpr uint32_t word_bits = 32;
constexpr uint32_t last_leading_zero = word_bits - 1;
constexpr uint64_t quantity_multiplier = 0xaaaaaaab;
constexpr uint32_t quantity_shift = 34;
constexpr size_t constructor_prefix_bytes = 0x20;

uint32_t leading_zeroes(uint32_t value)
{
    if (value == 0) return word_bits;
    uint32_t count = 0;
    constexpr uint32_t high_bit = uint32_t(1) << last_leading_zero;
    while ((value & high_bit) == 0) {
        value <<= 1;
        ++count;
    }
    return count;
}
uint32_t choose_level_count(uint32_t first, uint32_t second, uint32_t count)
{
    if (count != 0) return count;
    const uint32_t first_zeroes = leading_zeroes(first);
    const uint32_t second_zeroes = leading_zeroes(second);
    uint32_t minimum = last_leading_zero;
    if (second_zeroes < minimum) minimum = second_zeroes;
    if (first_zeroes < minimum) minimum = first_zeroes;
    return word_bits - minimum;
}
uint32_t align_quantity(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & (uint32_t(0) - alignment);
}
uint64_t pack_requirements(uint32_t quantity, uint32_t alignment)
{
    return uint64_t(quantity) | (uint64_t(alignment) << word_bits);
}
struct QuantityChoice {
    uint32_t value;
    uint32_t special_parameter;
    uint32_t reduced;
};
QuantityChoice choose_quantity(uint32_t secondary, uint8_t option, uint32_t quantity)
{
    QuantityChoice choice{quantity, uint32_t((secondary & special_parameter_flag) != 0), 0};
    if (option != 0 && (secondary & reduced_quantity_flag) != 0) {
        choice.value = uint32_t((uint64_t(quantity) * quantity_multiplier) >> quantity_shift);
        choice.reduced = 1;
    }
    return choice;
}
QuantityChoice read_quantity(const render::ResourceDescriptorNormalized &record)
{
    const uint32_t secondary = record.secondary_flags.get();
    const uint8_t option = record.option_byte.get();
    const uint32_t quantity = record.quantity.get();
    return choose_quantity(secondary, option, quantity);
}
uint32_t combine_record_parameters(render::ResourceDescriptorNormalized &record,
                            const QuantityChoice &choice)
{
    const uint32_t first = record.first_parameter.get();
    const uint32_t second = record.second_parameter.get();
    return render::resource_descriptor_calls::combine_record_parameters(first, second,
                                                                 choice.special_parameter);
}
// Each call rereads these fields after the preceding original helper. Callers
// choose the destination and derived argument at the native reload point.
uint64_t initialize_descriptor(render::ResourceDescriptorScratch &frame,
                               render::ResourceDescriptorStorage *destination, const QuantityChoice &choice,
                               uint32_t combined, bool alternate_parameter)
{
    auto &record = frame.normalized;
    const uint32_t first = record.first.get();
    const uint32_t second = record.second.get();
    const uint32_t requested_count = record.level_count.get();
    const uint32_t count = choose_level_count(first, second, requested_count);
    uint32_t first_derived = second_descriptor_parameter;
    if (!alternate_parameter) first_derived = record.first_derived.get();
    const uint32_t second_derived = record.second_derived.get();

    return render::initialize_pair_record_descriptor(
        destination, first, second, choice.value, count, first_derived,
        second_derived, combined, choice.reduced);
}
uint32_t prepare_loop_limit(render::ResourceDescriptorScratch &frame)
{
    auto &record = frame.normalized;
    const uint32_t count = record.level_count.get();
    if (count == 0) {
        const uint32_t first = record.first.get();
        const uint32_t second = record.second.get();
        return choose_level_count(first, second, 0);
    }
    return count;
}
struct AuxiliaryAccumulation { uint32_t quantity; uint32_t alignment; uint32_t status; };
AuxiliaryAccumulation accumulate_levels(render::ResourceDescriptorScratch &frame,
                                        render::ResourceDescriptorStorage *descriptor,
                                        render::ResourceDescriptorLevel *level_descriptor,
                                        bool second_auxiliary, uint32_t level_limit)
{
    uint32_t quantity = 0;
    uint32_t alignment = 0;
    if (level_limit == 0) return {quantity, alignment, 0};
    uint32_t level = 0;
    do {
        // Each level uses the current normalized quantity; helper writes can change it.
        const uint32_t multiplier = frame.normalized.quantity.get();
        uint32_t status = render::initialize_record_level_descriptor(
            level_descriptor, descriptor, level, 0);
        if (status == 0)
            status = render::calculate_record_geometry(&frame.geometry,
                                                                           level_descriptor);
        if (status == 0) {
            const uint64_t first_pair = render::Field<uint64_t>::read(frame.geometry.first_00.bytes);
            const uint64_t second = first_pair >> word_bits;
            const uint32_t third = frame.geometry.third_08.get();

            if (second_auxiliary) {
                status = render::resource_descriptor_calls::calculate_second_auxiliary(
                    &frame.optional_total, &frame.optional_unit, &frame.optional_first,
                    &frame.optional_second, first_pair, second, third, 0);
            } else {
                status = render::record_constructor_optional_first(
                    reinterpret_cast<uint64_t *>(frame.optional_total.bytes),
                    reinterpret_cast<uint32_t *>(frame.optional_unit.bytes),
                    reinterpret_cast<uint32_t *>(frame.optional_first.bytes),
                    reinterpret_cast<uint32_t *>(frame.optional_second.bytes),
                    static_cast<uint32_t>(first_pair), static_cast<uint32_t>(second), third, 0);
            }
        }
        if (status != 0) {
            render::resource_descriptor_calls::report_record_normalization_error(status);
            return {0, 0, mapped_auxiliary_failure};
        }
        // Both helpers publish a QWORD here; this caller consumes only its
        // low DWORD, then the unit DWORD. Other output bytes remain live.
        uint32_t low_total;
        std::memcpy(&low_total, frame.optional_total.bytes, sizeof(low_total));
        const uint32_t unit = frame.optional_unit.get();
        if (unit != 0) quantity = align_quantity(quantity, unit) + low_total * multiplier;
        if (unit > alignment) alignment = unit;
        ++level;
    } while (level < level_limit);
    return {quantity, alignment, 0};
}
}  // namespace

extern "C" uint64_t bb_render_prepare_resource_descriptor(
    render::ResourceDescriptorRequirements *requirements,
    render::ResourceDescriptorStorage *descriptor,
    const render::ResourceDescriptorParameters *parameters,
    const render::ResourceDescriptorOptional *optional,
    uintptr_t inherited_initial, uintptr_t inherited_owner)
{
    using namespace render;
    ResourceDescriptorScratch frame;

    resource_descriptor_calls::normalize(&frame.normalized, parameters, optional, 0,
                                          inherited_initial, inherited_owner);
    auto &record = frame.normalized;
    const uint32_t normalized_status = record.status.get();
    if (normalized_status != 0) return uint64_t(normalized_status);

    const uint32_t flags = record.flags.get();
    const uint32_t secondary = record.secondary_flags.get();
    const bool no_special_parameter = (secondary & special_parameter_flag) == 0;
    uint8_t first_auxiliary_enabled = 0;
    bool special_constructor = false;
    if ((flags & auxiliary_group_flag) != 0) {
        if (!no_special_parameter) {
            special_constructor = record.first_parameter.get() > 1;
            first_auxiliary_enabled = 1;
        } else {
            first_auxiliary_enabled = uint8_t((secondary & first_auxiliary_flag) >>
                                               first_auxiliary_shift);
        }
    }
    uint8_t second_descriptor_enabled = 0;
    uint32_t predicate_word = 0;
    if ((flags & auxiliary_descriptor_flag) != 0) {
        const uint32_t selector = record.selector.get();
        second_descriptor_enabled = resource_descriptor_calls::record_auxiliary_predicate(flags, selector);
        predicate_word = no_special_parameter
                         ? (record.secondary_flags.get() >> secondary_byte_shift) & secondary_byte_mask
                         : 1;
    }
    frame.first_auxiliary_pair.quantity.set(0);
    frame.first_auxiliary_pair.alignment.set(0);
    frame.constructor_pair.set_packed(0);

    uint64_t primary_result;
    uint32_t secondary_alignment = 0;
    uint64_t second_descriptor_result = 0, second_descriptor_alignment = 0;
    uint32_t second_auxiliary_total = 0;
    uint32_t second_auxiliary_alignment = 0;
    if (special_constructor) {
        std::memset(&frame.geometry, 0, constructor_prefix_bytes);
        frame.geometry.third_alignment_28.set(0);
        Field<uint64_t>::write(frame.geometry.first_alignment_20.bytes, 0);
        const uint32_t first_parameter = resource_descriptor_calls::choose_constructor_parameter(
            record.first_parameter.get(), 1);
        const uint32_t combine_first = record.first_parameter.get();
        const uint32_t combine_second = record.second_parameter.get();
        const uint32_t combined = resource_descriptor_calls::combine_record_parameters(
            combine_first, combine_second, 1);
        const uint32_t first = record.first.get();
        const uint32_t second = record.second.get();
        const uint32_t quantity = record.quantity.get();
        const uint32_t first_derived = record.first_derived.get();
        const uint32_t second_derived = record.second_derived.get();

        primary_result = resource_descriptor_calls::construct_record(
            &frame.geometry, first, second, quantity, first_derived, second_derived,
            first_parameter, combined, &frame.first_auxiliary_pair, &frame.constructor_pair);
        const uint32_t conversion_flags = (record.secondary_flags.get() & reduced_quantity_flag) >> 2;
        resource_descriptor_calls::convert_constructor_record(descriptor, &frame.geometry,
                                                               conversion_flags);
    } else {
        const QuantityChoice primary_quantity = read_quantity(record);
        const uint32_t combined = combine_record_parameters(record, primary_quantity);
        primary_result = initialize_descriptor(frame, descriptor, primary_quantity, combined, false);

        if (second_descriptor_enabled != 0) {
            const uint32_t current_secondary = record.secondary_flags.get();
            const uint8_t option = record.option_byte.get();
            second_descriptor_alignment = 0;
            const uint32_t quantity = record.quantity.get();
            const QuantityChoice choice = choose_quantity(current_secondary, option, quantity);
            const uint32_t second_combined = combine_record_parameters(record, choice);
            const uint32_t selector = record.selector.get();
            const uint32_t current_flags = record.flags.get();
            uint64_t second_result = 0;
            if (resource_descriptor_calls::record_auxiliary_predicate(current_flags, selector) != 0) {
                second_result = initialize_descriptor(frame, descriptor, choice, second_combined, true);
                second_descriptor_alignment = second_result >> word_bits;
            }
            second_descriptor_result = second_result;
        } else {
            second_descriptor_alignment = 0;
            second_descriptor_result = 0;
        }

        if (first_auxiliary_enabled != 0) {
            const uint32_t level_limit = prepare_loop_limit(frame);
            std::memset(&frame.first_temporary, 0, sizeof(frame.first_temporary));
            const QuantityChoice choice = read_quantity(record);
            const uint32_t auxiliary_combined = combine_record_parameters(record, choice);
            initialize_descriptor(frame, &frame.first_temporary, choice, auxiliary_combined, false);
            const AuxiliaryAccumulation accumulation = accumulate_levels(
                frame, &frame.first_temporary, &frame.first_level, false, level_limit);
            if (accumulation.status != 0) return uint64_t(accumulation.status);
            frame.first_auxiliary_pair.set_packed(pack_requirements(accumulation.quantity,
                                                                   accumulation.alignment));
        }
        if (predicate_word != 0) {
            const uint32_t level_limit = prepare_loop_limit(frame);
            std::memset(&frame.second_temporary, 0, sizeof(frame.second_temporary));
            const QuantityChoice choice = read_quantity(record);
            const uint32_t auxiliary_combined = combine_record_parameters(record, choice);
            initialize_descriptor(frame, &frame.second_temporary, choice, auxiliary_combined, false);
            const AuxiliaryAccumulation accumulation = accumulate_levels(
                frame, &frame.second_temporary, &frame.second_level, true, level_limit);
            if (accumulation.status != 0) return uint64_t(accumulation.status);
            second_auxiliary_total = accumulation.quantity;
            second_auxiliary_alignment = accumulation.alignment;
        }
        secondary_alignment = uint32_t(second_descriptor_alignment);
        if (secondary_alignment != 0) {
            const uint32_t aligned = align_quantity(uint32_t(primary_result), secondary_alignment);
            const uint64_t second_result = second_descriptor_result;
            primary_result = pack_requirements(aligned + uint32_t(second_result),
                                               uint32_t(primary_result >> word_bits));
        }
    }

    uint32_t total = uint32_t(primary_result);
    uint32_t alignment = uint32_t(primary_result >> word_bits);
    if (secondary_alignment > alignment) alignment = secondary_alignment;
    requirements->packed.set(pack_requirements(total, alignment));

    // Publish the four requirement folds in order. Each later fold reads the
    // corresponding auxiliary pair only after the previous output publication.
    uint32_t next_alignment = frame.first_auxiliary_pair.alignment.get();
    if (next_alignment != 0) {
        total = align_quantity(total, next_alignment);
        total += frame.first_auxiliary_pair.quantity.get();
    }
    if (alignment > next_alignment) next_alignment = alignment;
    alignment = next_alignment;
    requirements->packed.set(pack_requirements(total, alignment));

    next_alignment = frame.constructor_pair.alignment.get();
    if (next_alignment != 0) {
        total = align_quantity(total, next_alignment);
        total += frame.constructor_pair.quantity.get();
    }
    if (alignment > next_alignment) next_alignment = alignment;
    alignment = next_alignment;
    requirements->packed.set(pack_requirements(total, alignment));

    if (second_auxiliary_alignment != 0)
        total = align_quantity(total, second_auxiliary_alignment) + second_auxiliary_total;
    if (alignment > second_auxiliary_alignment) second_auxiliary_alignment = alignment;
    requirements->packed.set(pack_requirements(total, second_auxiliary_alignment));
    return uint64_t(0);
}
