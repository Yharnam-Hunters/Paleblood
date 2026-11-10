// SPDX-License-Identifier: GPL-2.0-or-later
// Process two ordered descriptor banks for the presentation shader. Empty work
// skips the final clears; nonempty work consumes the whole work word.
// Allocation failure still forwards or publishes NULL, rather than returning a status.
#include <bit>
#include <cstring>

#include "present_shader_secondary_state.h"
#include "shader_secondary_calls.h"

namespace {
using namespace render;
constexpr uint32_t first_stage = 2, second_stage = 1;
constexpr uint32_t word_bits = 32, entry_byte_bits = 8, scalar_origin = 16;
constexpr uint32_t first_byte_shift = entry_byte_bits, second_byte_shift = entry_byte_bits * 2;
constexpr uint32_t count_byte_shift = entry_byte_bits * 3;
constexpr uint32_t work_bits = 64, constant_word = 32;
constexpr uint64_t second_resource_mask = 0x00000ff000000000;
constexpr uint64_t fourth_resource_mask = 0x0000000fffff0000;
constexpr unsigned second_resource_shift = 36, fourth_resource_shift = 16;
constexpr uint32_t record32_bytes = 32, record16_bytes = 16;
constexpr uint32_t short_words = 4, long_words = 8;

struct SecondaryBank {
    uint32_t stage;
    Field<uint64_t> *work;
    ShaderSecondaryMaskPair *resources;
    Field<uint64_t> *other_resources;
    ShaderSecondaryMaskPair *pending;
    Field<uint64_t> *other_pending;
    ShaderSecondaryRecords32 *first;
    ShaderSecondaryRecords32 *second;
    ShaderSecondaryRecords16 *third;
    ShaderSecondaryRecords16 *fourth;
    ShaderSecondaryScalarArray *values;
};

struct SelectedSpan { uint32_t first; uint32_t count; };
enum class SpanSource { first, second, third, fourth };
enum class SecondaryEntryKind : uint8_t {
    first_descriptor16 = 0, first_descriptor32 = 1,
    second_descriptor16 = 2, second_descriptor32 = 3,
    third_sampler16 = 4, fourth_descriptor16 = 5, constant_word = 6,
    cached_address = 7,
    first_address_span = 8, second_address_span = 9,
    third_address_span = 10, fourth_address_span = 11,
    ignored_12 = 12,
    first_four_words = 13, first_eight_words = 14,
    second_four_words = 15, second_eight_words = 16,
    third_four_words = 17, fourth_four_words = 18,
    constant_scalar = 19, cached_pointer_words = 20,
    first_pointer_span = 21, second_pointer_span = 22,
    third_pointer_span = 23, fourth_pointer_span = 24,
    ignored_25 = 25, scalar_upload = 26,
};
struct CopiedSpan { bool selected; uint8_t *address; };

ShaderUploadArena *upload_arena(PacketBuffer *commands)
{
    auto *view = reinterpret_cast<ShaderSecondaryCommandView *>(commands);
    return reinterpret_cast<ShaderUploadArena *>(&view->upload_arena_b3d0);
}

uint8_t *allocate_upload(PacketBuffer *commands, uint64_t bytes)
{
    // The helper receives a private command-pointer slot, not the caller's variable.
    PacketBuffer *private_commands = commands;
    return allocate_shader_upload(upload_arena(commands), &private_commands, bytes);
}

uint8_t *cached_upload(ShaderSecondaryState *state, PacketBuffer *commands)
{
    uint8_t *address = state->cached_upload_b360.get();
    if (address == nullptr) {
        address = allocate_upload(commands, sizeof(state->upload_snapshot));
        state->cached_upload_b360.set(address);
        if (address != nullptr) {
            copy_bytes(address, state->upload_snapshot, sizeof(state->upload_snapshot));
            // A copy/alias can affect the cache field; the code reloads it here.
            address = state->cached_upload_b360.get();
        }
    }
    return address;
}

SelectedSpan one_word_span(uint64_t mask)
{
    if (mask == 0) return {0, 0};
    const uint32_t first = std::countr_zero(mask);
    const uint32_t count = work_bits - first - std::countl_zero(mask);
    return {first, count};
}

SelectedSpan selected_span(const SecondaryBank &bank, SpanSource source)
{
    if (source == SpanSource::first) {
        const uint64_t low = bank.resources->low.get();
        const uint64_t high = bank.resources->high.get();
        if (low == 0 && high == 0) return {0, 0};
        const uint32_t first = low != 0 ? std::countr_zero(low) : work_bits + std::countr_zero(high);
        const uint32_t last = high != 0 ? work_bits * 2 - 1 - std::countl_zero(high)
                                      : work_bits - 1 - std::countl_zero(low);
        return {first, last + 1 - first};
    }
    if (source == SpanSource::second)
        return one_word_span((bank.other_resources->get() & second_resource_mask) >> second_resource_shift);
    if (source == SpanSource::third) {
        // This path reads only a WORD, unlike the adjacent QWORD mask paths.
        uint16_t low;
        std::memcpy(&low, bank.other_resources->bytes, sizeof(low));
        return one_word_span(low);
    }
    return one_word_span((bank.other_resources->get() & fourth_resource_mask) >> fourth_resource_shift);
}

CopiedSpan copy_selected_span(const SecondaryBank &bank, PacketBuffer *commands, SpanSource source)
{
    const SelectedSpan span = selected_span(bank, source);
    if (span.count == 0) return {false, nullptr};
    const bool wide = source == SpanSource::first || source == SpanSource::second;
    const uint32_t stride = wide ? record32_bytes : record16_bytes;
    const uint64_t offset = uint64_t(span.first) * stride;
    const uint64_t bytes = uint64_t(span.count) * stride;
    uint8_t *address = allocate_upload(commands, offset + bytes);
    if (address != nullptr) {
        const void *input = bank.first->address(span.first);
        if (source == SpanSource::second) input = bank.second->address(span.first);
        else if (source == SpanSource::third) input = bank.third->address(span.first);
        else if (source == SpanSource::fourth) input = bank.fourth->address(span.first);
        // Prefix slots below first remain uninitialized. Keep the real memcpy boundary.
        void *destination = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(address) + offset);
        copy_bytes(destination, input, bytes);
    }
    return {true, address};
}

void store_address(const SecondaryBank &bank, uint32_t slot, const uint8_t *address)
{
    const uint64_t bits = reinterpret_cast<uintptr_t>(address);
    const int32_t first = int32_t(slot) - int32_t(scalar_origin);
    bank.values->set_signed(first, static_cast<uint32_t>(bits));
    bank.values->set_signed(first + 1, static_cast<uint32_t>(bits >> word_bits));
}

template <uint32_t Words> void copy_record_words(const SecondaryBank &bank,
    const ShaderSecondaryRecords<Words> *source, uint32_t index, uint32_t slot, uint32_t count)
{
    const int32_t first = int32_t(slot) - int32_t(scalar_origin);
    for (uint32_t word = 0; word < count; ++word) {
        // Read each DWORD after the preceding publication; source and destination may overlap.
        const uint32_t value = source->word(index, word);
        bank.values->set_signed(first + int32_t(word), value);
    }
}

void process_entry(ShaderSecondaryState *state, PacketBuffer *commands,
                   const SecondaryBank &bank, uint32_t entry)
{
    const uint8_t type = static_cast<uint8_t>(entry);
    const uint32_t slot = static_cast<uint8_t>(entry >> first_byte_shift);
    const uint32_t index = static_cast<uint8_t>(entry >> second_byte_shift);
    switch (static_cast<SecondaryEntryKind>(type)) {
    case SecondaryEntryKind::first_descriptor16: emit_shader_descriptor16(commands, bank.stage, slot, bank.first->address(index)); break;
    case SecondaryEntryKind::first_descriptor32: emit_shader_descriptor32(commands, bank.stage, slot, bank.first->address(index)); break;
    case SecondaryEntryKind::second_descriptor16: emit_shader_descriptor16(commands, bank.stage, slot, bank.second->address(index)); break;
    case SecondaryEntryKind::second_descriptor32: emit_shader_descriptor32(commands, bank.stage, slot, bank.second->address(index)); break;
    case SecondaryEntryKind::third_sampler16: emit_shader_sampler16(commands, bank.stage, slot, bank.third->address(index)); break;
    case SecondaryEntryKind::fourth_descriptor16: emit_shader_descriptor16(commands, bank.stage, slot, bank.fourth->address(index)); break;
    case SecondaryEntryKind::constant_word: emit_shader_word(commands, bank.stage, slot, constant_word); break;
    case SecondaryEntryKind::cached_address: emit_shader_address(commands, bank.stage, slot, cached_upload(state, commands)); break;
    case SecondaryEntryKind::first_address_span: case SecondaryEntryKind::second_address_span: case SecondaryEntryKind::third_address_span: case SecondaryEntryKind::fourth_address_span: {
        constexpr uint8_t first_span_type = static_cast<uint8_t>(SecondaryEntryKind::first_address_span);
        const auto source = static_cast<SpanSource>(type - first_span_type);
        const CopiedSpan copy = copy_selected_span(bank, commands, source);
        if (copy.selected) emit_shader_address(commands, bank.stage, slot, copy.address);
        break;
    }
    case SecondaryEntryKind::first_four_words: copy_record_words(bank, bank.first, index, slot, short_words); break;
    case SecondaryEntryKind::first_eight_words: copy_record_words(bank, bank.first, index, slot, long_words); break;
    case SecondaryEntryKind::second_four_words: copy_record_words(bank, bank.second, index, slot, short_words); break;
    case SecondaryEntryKind::second_eight_words: copy_record_words(bank, bank.second, index, slot, long_words); break;
    case SecondaryEntryKind::third_four_words: copy_record_words(bank, bank.third, index, slot, short_words); break;
    case SecondaryEntryKind::fourth_four_words: copy_record_words(bank, bank.fourth, index, slot, short_words); break;
    case SecondaryEntryKind::constant_scalar: bank.values->set_signed(int32_t(slot) - int32_t(scalar_origin), constant_word); break;
    case SecondaryEntryKind::cached_pointer_words: store_address(bank, slot, cached_upload(state, commands)); break;
    case SecondaryEntryKind::first_pointer_span: case SecondaryEntryKind::second_pointer_span: case SecondaryEntryKind::third_pointer_span: case SecondaryEntryKind::fourth_pointer_span: {
        constexpr uint8_t first_pointer_span_type = static_cast<uint8_t>(SecondaryEntryKind::first_pointer_span);
        const auto source = static_cast<SpanSource>(type - first_pointer_span_type);
        const CopiedSpan copy = copy_selected_span(bank, commands, source);
        if (copy.selected) store_address(bank, slot, copy.address);
        break;
    }
    case SecondaryEntryKind::scalar_upload: {
        const uint64_t bytes = uint64_t(entry >> count_byte_shift) * sizeof(uint32_t);
        uint8_t *address = allocate_upload(commands, bytes);
        if (address != nullptr) copy_bytes(address, bank.values->address(index), bytes);
        emit_shader_address(commands, bank.stage, slot, address);
        break;
    }
    default: break;  // Types 12, 25 and values above 26 still consume their local work bit.
    }
}

void process_bank(ShaderSecondaryState *state, PacketBuffer *commands,
                  const ShaderSecondaryDescriptorView *descriptor, const SecondaryBank &bank)
{
    uint64_t work = bank.work->get();
    const uint32_t count = descriptor->count_e8.get();
    if (count < work_bits) work &= (uint64_t(1) << count) - 1;
    if (work == 0) return;
    const uint8_t *entries = descriptor->entries_e0.get();
    do {
        const uint32_t bit = std::countr_zero(work);
        uint32_t entry;
        const uintptr_t source = reinterpret_cast<uintptr_t>(entries) + uint64_t(bit) * sizeof(entry);
        std::memcpy(&entry, reinterpret_cast<const void *>(source), sizeof(entry));
        process_entry(state, commands, bank, entry);
        work -= uint64_t(1) << bit;
    } while (work != 0);

    // Reload masks after every call, then clear the whole work word even
    // when the initial descriptor count excluded some of its bits.
    bank.pending->clear_removed(*bank.resources);
    const uint64_t removed = bank.other_resources->get();
    const uint64_t pending = bank.other_pending->get();
    bank.other_pending->set(pending & ~removed);
    bank.work->set(0);
}
}  // namespace

extern "C" void bb_render_configure_record_shader_secondary(
    render::ShaderSecondaryState *state, render::PacketBuffer *commands,
    const render::ShaderSecondaryDescriptorView *descriptor,
    const render::ShaderSecondaryDescriptorView *optional_descriptor)
{
    using namespace render;
    auto *command_view = reinterpret_cast<ShaderSecondaryCommandView *>(commands);
    if (state->command_mode_00.get() != 0) select_shader_command_mode(commands, 0);
    flush_shader_preparation(&command_view->preparation_b468, commands);
    const SecondaryBank first{first_stage, &state->work_30, &state->resources_60, &state->resources_70,
        &state->pending_18, &state->pending_28, &state->records_a_28e0, &state->records_b_88e0,
        &state->records_c_94e0, &state->records_d_9ae0, &state->values_a8a0};
    process_bank(state, commands, descriptor, first);
    if (optional_descriptor != nullptr) {
        const SecondaryBank second{second_stage, &state->work_1930, &state->resources_1960, &state->resources_1970,
            &state->pending_1918, &state->pending_1928, &state->records_a_68e0, &state->records_b_90e0,
            &state->records_c_98e0, &state->records_d_9fe0, &state->values_a6a0};
        process_bank(state, commands,
            optional_descriptor, second);
    }
}
