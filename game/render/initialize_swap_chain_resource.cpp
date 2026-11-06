// SPDX-License-Identifier: GPL-2.0-or-later
// Validate resource parameters, prepare storage and allocate its resource wrapper.
// Allocation failure clears the output and releases storage supplied by preparation.
#include "swap_chain_resource_init.h"

namespace {
constexpr uint32_t resource_word_bits = 32;
constexpr uint32_t resource_word_high_bit = 0x80000000u;
constexpr uint32_t maximum_fallback_zeroes = 31;
constexpr uint32_t multi_count_kind = 2;
constexpr uint32_t odd_selection_disabled_flag = 2;
constexpr uint32_t selected_kind_disabled_flags = 0x18;
constexpr uint32_t unsupported_status = 8;
constexpr uint32_t allocation_failed_status = 9;
constexpr uint32_t primary_rejected_bits = 0x17;
constexpr uint32_t secondary_rejected_bits = 0x38;
constexpr uint32_t allow_first_auxiliary_flag = 0x20;
constexpr uint32_t allow_second_auxiliary_flag = 0x40;
constexpr uint32_t request_second_auxiliary_flag = 0x100;
constexpr uint32_t request_first_auxiliary_flag = 0x200;
constexpr uint32_t secondary_pair_mask = 0x8100;
constexpr uint32_t secondary_pair_rejected = 0x8000;
constexpr uint32_t high_nibble_mask = 0xf0;
constexpr uint32_t count_requirement_bit = 4;
constexpr uint32_t minimum_count = 6;
constexpr uint32_t capability_flag_maximum = 0xff;
constexpr uint32_t capability_selection_maximum = 3;
constexpr uint32_t capability_secondary_maximum = 0xffff;
constexpr uint32_t conflicting_low_bit = 4;
constexpr uint32_t secondary_allowance_conflict = 0x60;
constexpr uint32_t high_option_conflict = 0xc0;
constexpr uint32_t masked_option_conflict = 0x90;
constexpr uint32_t object_initial_state = 1;
constexpr uint64_t object_alignment = 8;

enum class CompatibilityKind : uint32_t { unselected, initial_data, odd_selection, selected_without_primary_flags };

uint32_t leading_zero_count(uint32_t value)
{
    if (value == 0) return resource_word_bits;
    uint32_t count = 0;
    while ((value & resource_word_high_bit) == 0) {
        value <<= 1;
        ++count;
    }
    return count;
}

}  // namespace

extern "C" uint32_t bb_render_initialize_swap_chain_resource(
    render::SwapResourceServices *services, render::SwapResourceOutput *output,
    render::SwapResourceParameters *parameters, render::SwapResourceInitialEntry *initial,
    render::SwapResourceOwner *allocator_override, render::SwapResourceOwner *owner_override,
    render::ResourceInfoOptions *options)
{
    using namespace render;
    SwapResourceInfo info;

    const uint32_t flags = parameters->flags.get();
    if ((flags & primary_rejected_bits) != 0) return unsupported_status;
    const uint32_t secondary_flags = parameters->secondary_flags.get();
    if ((secondary_flags & secondary_rejected_bits) != 0) return unsupported_status;

    if (initial != nullptr) {
        uint32_t count = parameters->level_count.get();
        if (count == 0) {
            const uint32_t first_zeroes = leading_zero_count(parameters->width.get());
            const uint32_t second_zeroes = leading_zero_count(parameters->height.get());
            uint32_t minimum_zeroes = second_zeroes < maximum_fallback_zeroes ? second_zeroes : maximum_fallback_zeroes;
            if (first_zeroes < minimum_zeroes) minimum_zeroes = first_zeroes;
            count = resource_word_bits - minimum_zeroes;
        }
        count *= parameters->entries_per_level.get();  // The entry-count product wraps at 32 bits.
        uintptr_t entry_address = reinterpret_cast<uintptr_t>(initial);
        for (uint32_t index = 0; index < count; ++index) {
            auto *entry = reinterpret_cast<SwapResourceInitialEntry *>(entry_address);
            if (entry->value.get() == 0) return unsupported_status;
            entry_address += sizeof(SwapResourceInitialEntry);
        }
    }
    if ((secondary_flags & count_requirement_bit) != 0 &&
        parameters->entries_per_level.get() < minimum_count) return unsupported_status;
    if ((flags & allow_second_auxiliary_flag) == 0 && (secondary_flags & request_second_auxiliary_flag) != 0)
        return unsupported_status;
    if ((secondary_flags & secondary_pair_mask) == secondary_pair_rejected)
        return unsupported_status;
    if ((flags & allow_first_auxiliary_flag) == 0 && (secondary_flags & request_first_auxiliary_flag) != 0)
        return unsupported_status;
    if (parameters->count_mode.get() >= multi_count_kind && parameters->level_count.get() != 1)
        return unsupported_status;

    // Load both defaults before checking compatibility; capability tests short-circuit.
    SwapResourceOwner *default_allocator = services->default_allocator.get();
    SwapResourceOwner *default_owner = services->default_owner.get();
    bool capability = services->first_capability.get() != 0 &&
                      services->second_capability.get() != 0 &&
                      services->third_capability.get() != 0;
    if (flags > capability_flag_maximum ||
        parameters->selection.get() > capability_selection_maximum ||
        secondary_flags > capability_secondary_maximum) capability = false;

    bool candidate = false;
    switch (static_cast<CompatibilityKind>(parameters->kind.get())) {
    case CompatibilityKind::unselected:
        if (parameters->selection.get() != 0) capability = false;
        candidate = capability;
        break;
    case CompatibilityKind::initial_data:
        if (initial != nullptr && parameters->selection.get() == 0 &&
            flags != 0 && (flags & high_nibble_mask) == 0) candidate = capability;
        break;
    case CompatibilityKind::odd_selection:
        if ((parameters->selection.get() & 1) != 0 && flags != 0 &&
            (flags & high_nibble_mask) == 0) {
            if ((secondary_flags & odd_selection_disabled_flag) != 0) capability = false;
            candidate = capability;
        }
        break;
    case CompatibilityKind::selected_without_primary_flags:
        if (flags == 0) {
            const uint32_t selection = parameters->selection.get();
            if (selection != 0) {
                if ((secondary_flags & selected_kind_disabled_flags) != 0) capability = false;
                candidate = capability;
            }
        }
        break;
    default:
        break;
    }

    // The initial 0x17 mask already rejects bit 0x04 and the 0x90 pair's bit 0x10.
    // Those two later combination tests are therefore redundant on every admitted path;
    // flags is a local copy that no helper can change before this decision.
    bool rejected = true;
    if (!((flags & conflicting_low_bit) != 0 && (flags & ~conflicting_low_bit) != 0) &&
        (flags & secondary_allowance_conflict) != secondary_allowance_conflict &&
        (flags & high_option_conflict) != high_option_conflict) {
        rejected = (flags & masked_option_conflict) == masked_option_conflict || !candidate;
    }
    SwapResourceOwner *allocator = default_allocator;
    if (allocator_override != nullptr) allocator = allocator_override;
    SwapResourceOwner *owner = default_owner;
    if (owner_override != nullptr) owner = owner_override;
    if (owner == nullptr) return unsupported_status;
    if (allocator == nullptr) return unsupported_status;
    if (rejected) return unsupported_status;

    const uint32_t status = swap_resource_calls::prepare_resource_info(
        &info, services, parameters, initial, allocator, owner,
        options);
    if (status != 0) return status;

    SwapResourceOwnerVtable *allocator_vtable = allocator->vtable.get();
    const SwapResourceAllocate allocate = allocator_vtable->allocate.get();
    SwapResourceObject *object = allocate(allocator, sizeof(SwapResourceObject), object_alignment);
    if (object != nullptr) {
        object->vtable.set(reinterpret_cast<uintptr_t>(swap_resource_calls::initial_vtable.address()));
        object->allocator.set(allocator);
        object->state.set(object_initial_state);
        object->vtable.set(reinterpret_cast<uintptr_t>(swap_resource_calls::final_vtable.address()));
        object->info.levels_and_layout.set(info.levels_and_layout.get());
        object->info.classification_and_dimensions.set(info.classification_and_dimensions.get());
        object->info.kind_and_secondary_flags.set(info.kind_and_secondary_flags.get());
        object->info.capacity_and_alignment.set(info.capacity_and_alignment.get());
        object->info.mode_and_required_bytes.set(info.mode_and_required_bytes.get());
        SwapResourceOwner *info_owner = info.owner.get();
        const uintptr_t info_resource = info.resource.get();
        object->info.resource.set(info_resource);
        object->info.owner.set(info_owner);
        output->object.set(object);
        return 0;
    }

    // Clear the output before releasing a resource reported by the helper.
    // Read the resource only when the helper supplied an owner.
    output->object.set(nullptr);
    SwapResourceOwner *info_owner = info.owner.get();
    if (info_owner != nullptr) {
        const uintptr_t info_resource = info.resource.get();
        if (info_resource != 0) {
            SwapResourceOwnerVtable *info_vtable = info_owner->vtable.get();
            const SwapResourceRelease release = info_vtable->release.get();
            release(info_owner, info_resource);
            info.resource.set(0);
        }
    }
    return allocation_failed_status;
}
