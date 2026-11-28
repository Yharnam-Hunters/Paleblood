// SPDX-License-Identifier: GPL-2.0-or-later
// Allocate swap-chain storage, register VideoOut buffers and initialize cached
// resources and tone curves. Failures retain the original partial publications.
#include "swap_chain_setup.h"
#include "../engine/scalar.h"

namespace render {
namespace {
constexpr uint32_t cached_record_kind = 3;
constexpr uint32_t supported_format_selector = 0x57;
constexpr uint32_t construction_failed = 5;
constexpr uint32_t unsupported_setup = 8;
constexpr uint32_t allocation_failed = 9;
constexpr uint32_t tiled_unit = 8;
constexpr uint32_t high_word_shift = 32;
constexpr uint32_t encoded_address_shift = 8;
constexpr uint32_t videoout_pixel_format = 0x80000000;
constexpr uint32_t invalid_video_handle = 0x80290001;
constexpr uint32_t allow_secondary_200_flag = 0x20;
constexpr uint32_t primary_resource_option = 0x08;
constexpr uint32_t padded_storage_flag = 0x80;
constexpr uint32_t resource_option_shift = 5;
constexpr uint32_t primary_option_shift = 2;
constexpr uint32_t address_slot_count = 16;
constexpr uint32_t cached_descriptor_control_flags = 0x20;
constexpr uint64_t curve_plane_bytes = setup_curve_entries * sizeof(float);
constexpr uint64_t curve_storage_bytes = curve_plane_bytes * SetupCurveOutput::curve_channel_count;
constexpr uint64_t allocation_tail_bytes = 8;

struct SetupInfoWords { Field<uint64_t> words[SetupState::info_copy_qwords]; };
struct SetupCurveHeap {
    Field<float> first[setup_curve_entries];
    Field<float> second[setup_curve_entries];
    Field<float> third[setup_curve_entries];
};
static_assert(sizeof(SetupInfoWords) == sizeof(SetupInfo));
static_assert(offsetof(SetupCurveHeap, second) == 0x1004);
static_assert(offsetof(SetupCurveHeap, third) == 0x2008);
static_assert(sizeof(SetupCurveHeap) == 0x300c);

SetupRecord *record_at(SetupState *state, uint32_t index)
{
    // Compute addresses at the full record stride without narrowing the product.
    // The caller supplies backing storage for every admitted index.
    const uintptr_t address = reinterpret_cast<uintptr_t>(state) +
        uint64_t(index) * sizeof(SetupRecord);
    return reinterpret_cast<SetupRecord *>(address);
}

Field<uint64_t> *address_at(Field<uint64_t> *addresses, uint32_t index)
{
    return reinterpret_cast<Field<uint64_t> *>(
        reinterpret_cast<uintptr_t>(addresses) + uint64_t(index) * sizeof(*addresses));
}

uint16_t read_low_word(const Field<uint32_t> &field)
{
    uint16_t value;
    std::memcpy(&value, field.bytes, sizeof(value));
    return value;
}

void release_owned(SetupOwner *owner, uintptr_t allocation)
{
    SetupOwnerVtable *const vtable = owner->vtable.get();
    const SetupRelease release = vtable->release_70.get();
    release(owner, allocation);
}

// Destroy and release the published object, if any; the caller-visible slot keeps its value.
void release_output_object(SetupObjectSlot *output, SetupOwner *manager)
{
    SetupObject *const object = output->value.get();
    if (object != nullptr) {
        swap_setup_calls::destroy_object(object);
        release_owned(manager, reinterpret_cast<uintptr_t>(object));
    }
}

void copy_record(SetupRecord *destination, const SetupRecord &source)
{
    destination->tail_28.set(source.tail_28.get());
    constexpr uint32_t low_pair_qwords = 2;
    for (uint32_t i = SetupRecord::payload_qword_count; i-- > low_pair_qwords;)
        destination->words[i].set(source.words[i].get());
    const uint64_t first = source.words[0].get();
    const uint64_t second = source.words[1].get();
    destination->words[1].set(second);
    destination->words[0].set(first);
}

uint64_t cleanup_video_setup(SetupState *state, SetupObjectSlot *output,
                             SetupOwner *manager)
{
    swap_setup_calls::clear_curve(&state->curve);
    swap_setup_calls::clear_cached_record(&state->cached_record);
    const uint64_t queue = state->event_queue.get();
    if (queue != 0) {
        swap_setup_calls::delete_event_queue(queue);
        state->event_queue.set(0);
    }
    const int32_t handle = state->video_handle.get();
    if (uint32_t(handle) != invalid_video_handle) {
        swap_setup_calls::video_close(handle);
        state->video_handle.set(int32_t(invalid_video_handle));
    }
    SetupOwner *const allocator = state->allocator_2c8.get();
    if (allocator != nullptr) {
        const uintptr_t storage = state->storage_2d0.get();
        if (storage != 0) {
            release_owned(allocator, storage);
            state->storage_2d0.set(0);
        }
    }
    release_output_object(output, manager);
    return construction_failed;
}
}  // namespace
}  // namespace render

extern "C" uint64_t bb_render_setup_swap_chain(
    render::SetupState *state, render::SetupObjectSlot *output,
    render::SetupResourceSlot *resource_output, render::SetupServices *services,
    render::SetupInfo *info,
    render::SetupOwner *manager, render::SetupOwner *allocator)
{
    using namespace render;
    Field<uint64_t> object_scratch;
    Field<uint32_t> mapped_selector;
    Field<uint32_t> unit_output;
    SetupRecord descriptor;
    SetupVideoAttribute video_attribute;
    SetupParameters parameters;
    SetupChannel channels[SetupCurveOutput::curve_channel_count];
    SetupTemporaryAllocator temporary;
    Field<uint64_t> addresses[address_slot_count];
    if (info->format_selector.get() != supported_format_selector) {
        return unsupported_setup;
    }
    SetupObject *const object = swap_setup_calls::create_object(1, 1, manager, services, info, manager);
    if (object == nullptr) {
        output->value.set(nullptr);
        return allocation_failed;
    }
    swap_setup_calls::initialize_object(object, &object_scratch);
    output->value.set(object);
    const uint32_t selector_status = swap_setup_calls::map_selector(
        &mapped_selector, info->format_selector.get());
    if (selector_status != 0) return selector_status;
    const uint32_t unit_status = swap_setup_calls::resolve_unit(
        &unit_output, 1, mapped_selector.get(), 1);
    if (unit_status != 0) {
        swap_setup_calls::note_unit_failure(unit_status);
        return unsupported_setup;
    }

    const uint32_t unit = unit_output.get();
    const uint32_t width = info->width.get();
    const uint32_t height = info->height.get();
    const uint32_t first = swap_setup_calls::first_parameter(info->first_constructor_parameter.get(), 0);
    const uint32_t second = swap_setup_calls::second_parameter(
        info->first_constructor_parameter.get(), info->second_constructor_parameter.get(), 0);
    const uint64_t packed = swap_setup_calls::construct_record(
        &descriptor, width, height,
        1, mapped_selector.get(), unit,
        first, second, 0, 0);
    if (uint32_t(packed) == 0) {
        return construction_failed;
    }
    // The buffer count is read again after each record copy.
    uint32_t count = info->buffer_count.get();
    for (uint32_t index = 0; index < count; ++index) {
        copy_record(record_at(state, index), descriptor);
        count = info->buffer_count.get();
    }

    const uint32_t alignment = uint32_t(packed >> high_word_shift);
    const uint32_t rounding_sum = uint32_t(packed) + alignment - 1;
    const uint32_t rounded = rounding_sum & (uint32_t(0) - alignment);
    const uint32_t allocation_bytes = rounded * count + uint32_t(allocation_tail_bytes);
    const SetupAllocate58 allocate = allocator->vtable.get()->allocate_58.get();
    const uintptr_t storage = allocate(allocator, uint64_t(allocation_bytes),
                                      uint64_t(alignment), uint64_t(rounding_sum));
    if (storage == 0) {
        release_output_object(output, manager);
        return allocation_failed;
    }
    if (swap_setup_calls::storage_admitted(storage) == 0) {
        release_owned(allocator, storage);
        release_output_object(output, manager);
        return unsupported_setup;
    }
    swap_setup_calls::fill_storage(storage, 0,
                                  uint64_t(allocation_bytes));
    uint32_t offset = 0;
    for (uint32_t index = 0; index < info->buffer_count.get(); ++index) {
        const uintptr_t address = storage + uint64_t(offset);
        const uint32_t encoded = swap_setup_calls::encode_address(address);
        swap_setup_calls::set_record_address(record_at(state, index), uintptr_t(encoded));
        offset += rounded;
    }

    state->manager_2c0.set(manager);
    state->allocator_2c8.set(allocator);
    state->storage_2d0.set(storage);
    state->service_2d8.set(swap_setup_calls::storage_service(services));
    state->zero_2e0.set(0);
    state->last_2e8.set(storage + uint64_t(allocation_bytes) - allocation_tail_bytes);
    state->video_handle.set(int32_t(invalid_video_handle));
    state->event_queue.set(0);
    auto *info_words = reinterpret_cast<SetupInfoWords *>(info);
    for (uint32_t index = 0; index < SetupState::info_copy_qwords; ++index) {
        state->info_copy[index].set(info_words->words[index].get());
    }
    const int32_t handle = swap_setup_calls::video_open(0, 0, 0, 0);
    state->video_handle.set(handle);
    if (handle < 0) {
        return cleanup_video_setup(state, output, manager);
    }
    const int32_t flip_status = swap_setup_calls::video_flip_rate(handle, 0);
    if (flip_status < 0) {
        return cleanup_video_setup(state, output, manager);
    }
    const int32_t queue_status = swap_setup_calls::create_event_queue(
        &state->event_queue, swap_setup_calls::queue_name.address()->bytes);
    if (queue_status < 0) {
        return cleanup_video_setup(state, output, manager);
    }
    const uint64_t queue = state->event_queue.get();
    const int32_t event_handle = state->video_handle.get();
    const int32_t event_status = swap_setup_calls::add_flip_event(queue, event_handle, 0);
    if (event_status < 0) {
        return cleanup_video_setup(state, output, manager);
    }
    const uint32_t tiled = unit == tiled_unit;
    const uint32_t attribute_width = info->width.get();
    const uint32_t attribute_height = info->height.get();
    swap_setup_calls::set_video_attribute(&video_attribute, videoout_pixel_format, tiled, 0,
                                         attribute_width, attribute_height,
                                         attribute_width);
    uint32_t registered_count = info->buffer_count.get();
    for (uint32_t index = 0; index < registered_count; ++index) {
        const uint32_t encoded = swap_setup_calls::get_record_address(record_at(state, index));
        address_at(addresses, index)->set(uint64_t(encoded) << encoded_address_shift);
        registered_count = info->buffer_count.get();
    }
    const int32_t register_handle = state->video_handle.get();
    const int32_t register_status = swap_setup_calls::register_video_buffers(
        register_handle, 0, addresses, registered_count, &video_attribute);
    if (register_status < 0) {
        return cleanup_video_setup(state, output, manager);
    }

    const uint32_t flags = info->resource_options.get();
    uint32_t selected = info->resource_selector_override.get();
    const uint32_t shifted = flags << resource_option_shift;
    const uint32_t mapped_flags = (shifted & allow_secondary_200_flag) |
        ((flags << primary_option_shift) & primary_resource_option) | (shifted & padded_storage_flag);
    if (selected == 0) selected = info->format_selector.get();
    parameters.width.set(info->width.get());
    parameters.height.set(info->height.get());
    parameters.level_count.set(1);
    parameters.entries_per_level.set(1);
    parameters.selector.set(selected);
    parameters.count_mode.set(1);
    parameters.layout_parameter.set(0);
    parameters.compatibility_kind.set(0);
    parameters.flags.set(mapped_flags);
    parameters.selection.set(0);
    parameters.secondary_flags.set(info->resource_secondary_flags.get());
    parameters.trailing_parameter.set(0);
    const uint32_t cached_status = swap_setup_calls::initialize_cached_record(
        services, resource_output, &parameters, nullptr, manager, allocator, 0);
    if (cached_status != 0) {
        // This failure deliberately has no generalized rollback.
        return cached_status;
    }
    SetupCachedRecord &cached = state->cached_record;
    cached.owner.set(nullptr);
    cached.storage.set(state->storage_2d0.get());
    cached.storage_mode.set(swap_setup_calls::current_storage_mode());
    cached.required_bytes.set(uint32_t(packed));
    cached.record_kind.set(cached_record_kind);
    cached.compatibility_kind.set(0);
    cached.selection.set(0);
    cached.resource_flags.set(cached_descriptor_control_flags);
    cached.secondary_flags.set(0);
    cached.classification.set(unit_output.get());
    cached.width.set(read_low_word(info->width));
    cached.height.set(read_low_word(info->height));
    cached.entries_per_level.set(1);
    cached.level_count.set(1);
    cached.selector.set(info->format_selector.bytes[0]);
    cached.first_constructor_parameter.set(read_low_word(info->first_constructor_parameter));
    cached.second_constructor_parameter.set(read_low_word(info->second_constructor_parameter));

    const void *const temporary_vtable = &swap_setup_calls::temporary_vtable.address()->first_virtual_function;
    temporary.vtable.set(temporary_vtable);
    temporary.token.set(swap_setup_calls::capture_upload_token());
    uintptr_t upload = swap_setup_calls::allocate_upload(&temporary, curve_storage_bytes);
    SetupOwner *fallback_owner = nullptr;
    if (manager != nullptr && upload == 0) {
        const SetupAllocate fallback = manager->vtable.get()->allocate_50.get();
        upload = fallback(manager, curve_storage_bytes);
        fallback_owner = manager;
    }
    uint32_t status = allocation_failed;
    if (upload != 0) {
        auto *heap = reinterpret_cast<SetupCurveHeap *>(upload);
        const float multiplier = swap_setup_calls::curve_multiplier.get();
        for (uint32_t index = 0; index < setup_curve_entries; ++index) {
            const float value = engine::multiply(engine::u64_to_float(uint64_t(index)), multiplier);
            for (Field<float> &component : state->curve.triplets[index]) component.set(value);
            heap->first[index].set(value);
            heap->second[index].set(value);
            heap->third[index].set(value);
        }
        uintptr_t plane = upload;
        for (SetupChannel &channel : channels) {
            channel.data.set(plane);
            channel.bytes.set(uint32_t(curve_plane_bytes));
            channel.zero.set(0);
            plane += curve_plane_bytes;
        }
        const uintptr_t curve_service = swap_setup_calls::curve_service(services);
        status = swap_setup_calls::initialize_curve(
            &state->curve, curve_service, swap_setup_calls::curve_settings.address(), channels,
            manager, allocator, 0);
        if (fallback_owner != nullptr) release_owned(fallback_owner, upload);
    }
    temporary.vtable.set(temporary_vtable);
    swap_setup_calls::release_upload_token(temporary.token.get());
    return status;
}
