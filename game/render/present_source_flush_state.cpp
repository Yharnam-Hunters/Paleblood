// SPDX-License-Identifier: GPL-2.0-or-later
// Emit dirty, non-null presentation slots from the selected bank. Skipped entries retain
// the cached count and flags. A processed entry refreshes both after the command helper and
// copies the then-current pending value into its cache, even if the helper changed it.
#include "present_source_flush_state.h"

namespace {
constexpr uint32_t bank_mask = 1;
constexpr uint32_t dirty_shift = 4;
constexpr uint32_t native_shift_mask = 31;
constexpr uint32_t byte_bits = 8;
constexpr uint32_t full_value_bits = 64;
constexpr uint32_t packed_value_bits = 32;
constexpr uint32_t packed_byte_count = 4;
constexpr uint64_t byte_mask = 0xff;

// Native indexing has no count<=8 guard. Keep its integer address arithmetic when an
// externally supplied backing object lets a larger index cross a declared array boundary.
template <typename T> T &indexed_native_slot(T *first, uint32_t index)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(first) +
                              static_cast<uintptr_t>(index) * sizeof(T);
    return *reinterpret_cast<T *>(address);
}

uint64_t pack_odd_bytes(uint64_t value)
{
    uint32_t result = 0;
    for (uint32_t byte = 0; byte < packed_byte_count; ++byte) {
        const uint32_t source_shift = (byte * 2 + 1) * byte_bits;
        result |= static_cast<uint32_t>((value >> source_shift) & byte_mask) << (byte * byte_bits);
    }
    return result;
}
}  // namespace

extern "C" void bb_render_present_source_flush_state(render::PresentSourcePreparation *state,
                                                      render::PresentSourceContext *context,
                                                      render::PresentSourceView *)
{
    using namespace render;
    uint32_t count = state->slot_count;
    if (count == 0) return;
    PacketBuffer *commands = &context->commands;
    uint32_t flags = state->flags;
    for (uint32_t index = 0; index < count; ++index) {
        const uintptr_t selected = indexed_native_slot(state->banks[flags & bank_mask].slots, index);
        if (selected == 0 || selected == std::numeric_limits<uintptr_t>::max()) continue;
        const uint32_t bit = uint32_t{1} << (index & native_shift_mask);
        if ((static_cast<uint8_t>(flags >> dirty_shift) & static_cast<uint8_t>(bit)) == 0) continue;

        uint64_t value = 0;
        const uint64_t sampled = indexed_native_slot(state->pending_values, index);
        const auto *slot = reinterpret_cast<const PresentSelectedSlot *>(selected);
        const uint32_t bits = render_present_encoding_units(slot->encoding) << 3;
        if (bits == full_value_bits) value = sampled;
        else if (bits == packed_value_bits) value = pack_odd_bytes(sampled);
        render_present_slot_emit(commands, index, &value);
        indexed_native_slot(state->cached_values, index) = indexed_native_slot(state->pending_values, index);
        // Only the low eight shifted bits can reach this path, so this clears exactly the
        // original mask's matching dirty bit while retaining every other freshly read flag.
        flags = state->flags & ~(bit << dirty_shift);
        state->flags = flags;
        count = state->slot_count;
    }
}
