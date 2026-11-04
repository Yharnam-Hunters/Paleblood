// SPDX-License-Identifier: GPL-2.0-or-later
// Append the seven-word counter-operation packet used by the submission delegate. The
// label's numeric range selects its encoding; operation bits and address bits are masked
// exactly here. The GPU meanings of the operation selectors remain outside this wrapper.
#include <cstring>

#include "command_packets.h"

namespace {
constexpr uint32_t counter_packet_words = 7;
constexpr uint32_t counter_packet_header = 0xc0053c00;
constexpr uint32_t counter_operation_mask = 0x7;
constexpr uint32_t wide_label_flag = 0x10;
constexpr uint32_t short_label_mask = 0xffff;
constexpr uint32_t wide_label_mask = 0xfffffffc;
constexpr uint32_t label_high_shift = 32;
constexpr uint32_t counter_packet_tail = 10;

struct CounterOperationPacket {
    uint32_t header;
    uint32_t control;
    uint32_t label_low;
    uint32_t label_high;
    uint32_t reference;
    uint32_t mask;
    uint32_t tail;
};
static_assert(offsetof(CounterOperationPacket, control) == 0x4);
static_assert(offsetof(CounterOperationPacket, label_low) == 0x8);
static_assert(offsetof(CounterOperationPacket, label_high) == 0xc);
static_assert(offsetof(CounterOperationPacket, reference) == 0x10);
static_assert(offsetof(CounterOperationPacket, mask) == 0x14);
static_assert(offsetof(CounterOperationPacket, tail) == 0x18);
static_assert(sizeof(CounterOperationPacket) == 0x1c);
}  // namespace

extern "C" void bb_render_command_emit_counter_operation(render::PacketBuffer *buffer,
                                                          uintptr_t label, uint32_t mask,
                                                          uint32_t operation, uint32_t reference)
{
    if (render::packet_available_words(buffer) < counter_packet_words) {
        const uint8_t granted = buffer->space_callback(buffer, counter_packet_words,
                                                       buffer->callback_argument);
        if (granted == 0) return;
    }

    const bool wide_label = label > short_label_mask;
    const uint32_t control = (operation & counter_operation_mask) |
                             (wide_label ? wide_label_flag : 0);
    const uint32_t label_low = static_cast<uint32_t>(label) &
                              (wide_label ? wide_label_mask : short_label_mask);
    const uint32_t label_high = static_cast<uint32_t>(label >> label_high_shift) & short_label_mask;
    const CounterOperationPacket packet{counter_packet_header, control, label_low, label_high,
                                        reference, mask, counter_packet_tail};
    // Copy bytes so command storage may alias the cursor field. Its final increment uses
    // the stored value after the packet write, as the original ADDQ does.
    std::memcpy(reinterpret_cast<void *>(buffer->cursor), &packet, sizeof(packet));
    buffer->cursor += counter_packet_words * render::packet_word_bytes;
}
