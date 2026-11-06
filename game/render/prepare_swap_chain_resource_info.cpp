// SPDX-License-Identifier: GPL-2.0-or-later
// Allocate resource storage and publish the descriptor before initializing its data.
// Resource-wrapper initialization calls this helper; later copy failures keep that publication.
#include "swap_chain_resource_info.h"

namespace {
constexpr uint32_t unsupported_status = 8;
constexpr uint32_t allocation_failed_status = 9;
constexpr uint32_t restricted_parameter = 16;
constexpr uint32_t restricted_parameter_flag = 0x400;
constexpr uint32_t first_allocator_flag = 0x4000;
constexpr uint32_t padded_allocation_flag = 0x80;
constexpr uint32_t allocation_padding = 23;
constexpr uint32_t allocation_rounding_mask = 0xfffffff8;
constexpr uint32_t minimum_alignment = 8;
constexpr uint32_t published_kind = 3;
constexpr uint32_t control_selector = 0xffffffff;

uint16_t low_word(const render::Field<uint32_t> &field)
{
    uint16_t value;
    std::memcpy(&value, field.bytes, sizeof(value));
    return value;
}
render::ResourceInfoInitialEntry *initial_entry(render::ResourceInfoInitialEntry *base,
                                               uint32_t index)
{
    return reinterpret_cast<render::ResourceInfoInitialEntry *>(
        reinterpret_cast<uintptr_t>(base) + uint64_t(index) * sizeof(*base));
}
void zero_qword(uintptr_t address)
{
    const uint64_t zero = 0;
    std::memcpy(reinterpret_cast<void *>(address), &zero, sizeof(zero));
}
constexpr uint32_t final_padding_qword = 8;
constexpr uint32_t preceding_padding_qword = 16;
}  // namespace

extern "C" uint32_t bb_render_prepare_swap_chain_resource_info(
    render::ResourceInfoOutput *output, render::ResourceInfoServices *services,
    render::ResourceInfoParameters *parameters, render::ResourceInfoInitialEntry *initial,
    render::ResourceInfoOwner *candidate_a, render::ResourceInfoOwner *candidate_b,
    render::ResourceInfoOptions *options)
{
    using namespace render;
    ResourceInfoRequirements requirements;
    ResourceInfoDescriptor descriptor;
    ResourceInfoLevelDescriptor temporary_descriptor;
    uint8_t size_slot[sizeof(uint64_t)];
    Field<uint64_t> offset;
    const uint32_t requested_layout = parameters->layout.get();
    const uint32_t secondary_flags = parameters->secondary_flags.get();
    if (requested_layout == restricted_parameter &&
        (secondary_flags & restricted_parameter_flag) == 0) return unsupported_status;

    ResourceInfoOwner *allocator = candidate_a;
    if ((secondary_flags & first_allocator_flag) == 0) allocator = candidate_b;
    const uint64_t zero = 0;
    std::memcpy(&requirements, &zero, sizeof(zero));
    ResourceInfoSuppliedParameter *optional_payload = nullptr;
    if (options != nullptr && options->payload_enabled.get() != 0)
        optional_payload = &options->payload;
    const uint32_t descriptor_status = resource_info_calls::prepare_descriptor(
        &requirements, &descriptor, parameters, optional_payload, initial, candidate_b);
    if (descriptor_status != 0) return descriptor_status;

    const uint32_t required_bytes = requirements.bytes.get();
    uint32_t allocation_alignment = requirements.alignment.get();
    uint32_t allocation_bytes = required_bytes;
    if (options != nullptr && options->capacity_enabled.get() != 0) {
        allocation_bytes = options->capacity.get();
        if (allocation_bytes < required_bytes) return unsupported_status;
        const uint32_t override_alignment = options->alignment.get();
        if (override_alignment < allocation_alignment) return unsupported_status;
        allocation_alignment = override_alignment;
    }
    const uint32_t allocation_flag = parameters->flags.get() & padded_allocation_flag;
    if (allocation_flag != 0) {
        allocation_bytes = (allocation_bytes + allocation_padding) & allocation_rounding_mask;
        if (allocation_alignment <= minimum_alignment) allocation_alignment = minimum_alignment;
    }
    const uint32_t levels = resource_info_calls::descriptor_levels(&descriptor);
    const ResourceInfoAllocate allocate = allocator->vtable.get()->allocate.get();
    const uintptr_t allocation = allocate(allocator, uint64_t(allocation_bytes),
                                          uint64_t(allocation_alignment));
    if (allocation == 0) return allocation_failed_status;
    const uint32_t level_count = levels + 1;
    if (allocation_flag != 0) {
        zero_qword(allocation + uint64_t(allocation_bytes) - final_padding_qword);
        zero_qword(allocation + uint64_t(allocation_bytes) - preceding_padding_qword);
    }
    output->owner.set(allocator);
    output->allocation.set(allocation);
    output->mode.set(resource_info_calls::current_mode());
    output->required_bytes.set(required_bytes);
    output->allocation_bytes.set(allocation_bytes);
    output->alignment.set(allocation_alignment);
    output->kind.set(published_kind);
    output->source_kind.set(parameters->compatibility_kind.bytes[0]);
    output->selection.set(parameters->selection.bytes[0]);
    output->flags.set(parameters->flags.bytes[0]);
    output->secondary_flags.set(parameters->secondary_flags.get());
    output->classification.set(resource_info_calls::descriptor_classification(&descriptor));
    output->width.set(low_word(parameters->width));
    output->height.set(low_word(parameters->height));
    output->entries_per_level.set(low_word(parameters->entries_per_level));
    output->levels.set(uint8_t(level_count));
    output->selector.set(parameters->selector.bytes[0]);
    output->layout.set(low_word(parameters->layout));
    output->layout_parameter.set(low_word(parameters->layout_parameter));

    // Options contents remain live across opaque calls and output publication.
    bool alternate_path = false;
    if (options != nullptr) alternate_path = options->alternate_path.get() != 0;
    if (initial == nullptr) return 0;
    if (resource_info_calls::needs_initial_copy(parameters->selector.get()) == 0)
        return 0;
    if (options != nullptr && options->span_copy_enabled.get() != 0) {
        const uintptr_t source = initial->data.get();
        resource_info_calls::copy_initial_span(allocation, source, uint64_t(required_bytes));
        return 0;
    }

    if (!alternate_path && resource_info_calls::storage_admitted(output->allocation.get()) != 0) {
        ResourceInfoControl *control = services->control.get();
        const ResourceInfoBeginProcessing operation = control->vtable.get()->begin_processing.get();
        operation(control, uint64_t(control_selector));
        ResourceInfoProcessor *processor = &services->processor;
        if (level_count != 0) {
            uint32_t current_count = parameters->entries_per_level.get();
            const uint32_t step = levels + 1;
            uint32_t outer = 0;
            do {
                uint32_t flattened = outer;
                uint32_t inner = 0;
                if (current_count != 0) {
                    do {
                        ResourceInfoInitialEntry *entry = initial_entry(initial, flattened);
                        resource_info_calls::process_initial_entry(
                            processor, output, entry, uint64_t(flattened), uint64_t(0));
                        ++inner;
                        current_count = parameters->entries_per_level.get();
                        flattened += step;
                    } while (inner < current_count);
                }
                ++outer;
            } while (outer < level_count);
        }
        resource_info_calls::finish_processing_first(processor);
        resource_info_calls::finish_processing_second(processor);
        ResourceInfoControl *final_control = services->control.get();
        const ResourceInfoEndProcessing final_operation = final_control->vtable.get()->end_processing.get();
        final_operation(final_control);
        return 0;
    }

    const uint8_t copy_admission = resource_info_calls::alternate_copy_admitted(output->allocation.get());
    if (level_count == 0) return 0;
    // This admission result is an exact-one token; other nonzero bytes decline.
    if (copy_admission != 1) return 0;
    uint32_t current_count = parameters->entries_per_level.get();
    uint32_t level = 0;
    do {
        uint32_t flattened = level;
        uint32_t inner = 0;
        if (current_count != 0) {
            do {
                offset.set(0);
                const uint32_t offset_status = resource_info_calls::calculate_level_offset(
                    &offset, size_slot, &descriptor, level, inner);
                if (offset_status != 0) {
                    resource_info_calls::note_offset_failure(offset_status);
                    return unsupported_status;  // Offset failure keeps the allocated storage.
                }
                const uintptr_t destination = uintptr_t(offset.get()) + allocation;
                resource_info_calls::initialize_level_descriptor(
                    &temporary_descriptor, &descriptor, level, inner);
                const uintptr_t source = initial_entry(initial, flattened)->data.get();
                const uint32_t copy_status = resource_info_calls::copy_initial_level(
                    destination, source,
                    &temporary_descriptor);
                if (copy_status != 0) {
                    const ResourceInfoRelease release = allocator->vtable.get()->release.get();
                    release(allocator, allocation);
                    return unsupported_status;  // Published output is not cleared.
                }
                ++inner;
                current_count = parameters->entries_per_level.get();
                flattened += (levels + 1);
            } while (inner < current_count);
        }
        ++level;
    } while (level < level_count);
    return 0;
}
