// SPDX-License-Identifier: GPL-2.0-or-later
// Write the selected CB_COLOR target's two clear-word registers with SET_CONTEXT_REG.
// The state-flush caller supplies the payload; its color format is not established here.
// Prefix writes precede an aliased input read, and the final cursor increment uses its
// then-current stored value because the packet itself can overlap the buffer prefix.
#include <cstring>

#include "command_reservation.h"
#include "present_slot_emit.h"
#include "render_observe.h"

namespace {
constexpr uint32_t packet_words = 4;
constexpr uint32_t set_context_reg_two_values_header = 0xc0026900;
constexpr uint32_t color_target_register_stride = 15;
constexpr uint32_t cb_color0_clear_word0 = 0x323;
constexpr unsigned clear_word1_shift = 32;  // CLEAR_WORD1 is the payload's high half
}  // namespace

extern "C" void bb_render_present_slot_emit(render::PacketBuffer *buffer, uint32_t index,
                                             const uint64_t *value)
{
    uintptr_t cursor;
    if (!render::reserve_words(buffer, packet_words, cursor)) return;

    const uint32_t register_index = index * color_target_register_stride + cb_color0_clear_word0;
    // Byte copies preserve aliasing between command storage, the input and the buffer fields.
    std::memcpy(reinterpret_cast<void *>(cursor + offsetof(render::PresentSlotPacket, header)),
                &set_context_reg_two_values_header, sizeof(set_context_reg_two_values_header));
    std::memcpy(reinterpret_cast<void *>(cursor + offsetof(render::PresentSlotPacket, register_index)),
                &register_index, sizeof(register_index));
    uint64_t loaded_value;
    std::memcpy(&loaded_value, value, sizeof(loaded_value));
    // the renderer applies fast clears with these words (runtime/render_executor.h)
    render::observe_context_register(buffer, cursor, register_index, static_cast<uint32_t>(loaded_value));
    render::observe_context_register(buffer, cursor, register_index + 1, static_cast<uint32_t>(loaded_value >> clear_word1_shift));
    std::memcpy(reinterpret_cast<void *>(cursor + offsetof(render::PresentSlotPacket, value)),
                &loaded_value, sizeof(loaded_value));
    render::set_packet_cursor(buffer, render::packet_cursor(buffer) + sizeof(render::PresentSlotPacket));
}
