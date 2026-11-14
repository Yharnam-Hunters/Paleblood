// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the two writers of the cache-coherence packet that shader preparation emits
// (shader_preparation.h): a type-3 ACQUIRE_MEM packet of 7 words. They reserve the words with the
// shared reservation (command_reservation.h: the space callback is asked when too few are left; a
// refusal writes nothing, a grant is trusted without checking the limit again), write the packet
// at the cursor, then move the cursor past it (reading the cursor again: the packet may overlap
// the buffer's own prefix).
//
// The packet: the header; the coherence control; the size (low, then high word 0); the base (low,
// then high word 0); the poll interval, 10. The control word packs the cache actions (bits 0, 1
// and 3 move up to 15, 16 and 18; bits 4 and 5 to 22 and 23), the targets (31 bits) and a flag in
// the top bit; other action bits are dropped.
// - render_emit_shader_preparation_range_01475e10 (0x01475e10): a range of memory; the targets
//   are two masks or'ed together.
// - render_emit_shader_preparation_event_01477f40 (0x01477f40): all of memory (size 1, base 0).
#include <cstdint>
#include <cstring>

#include "command_packets.h"
#include "command_reservation.h"

namespace {

using render::PacketBuffer;

constexpr uint32_t acquire_mem_header = 0xc0055800;  // type 3, ACQUIRE_MEM, 6 more words
constexpr uint32_t poll_interval = 10;
constexpr uint32_t whole_memory_size = 1;

constexpr uint32_t action_low_bits = 0xb, action_low_shift = 15;
constexpr uint32_t action_high_bits = 0x30, action_high_shift = 18;
constexpr uint32_t target_bits = 0x7fffffff, flag_shift = 31;

struct AcquireMemPacket {
    uint32_t header;
    uint32_t control;
    uint32_t size_low, size_high;
    uint32_t base_low, base_high;
    uint32_t poll_interval;
};
static_assert(sizeof(AcquireMemPacket) == 7 * render::packet_word_bytes);

constexpr uint32_t packet_words = sizeof(AcquireMemPacket) / render::packet_word_bytes;

uint32_t coherence_control(uint32_t actions, uint32_t targets, uint32_t flag)
{
    return (actions & action_low_bits) << action_low_shift | (actions & action_high_bits) << action_high_shift |
           (targets & target_bits) | flag << flag_shift;
}

void emit(PacketBuffer *buffer, uint32_t control, uint32_t size, uint32_t base)
{
    uintptr_t cursor;
    if (!render::reserve_words(buffer, packet_words, cursor))
        return;
    const AcquireMemPacket packet{acquire_mem_header, control, size, 0, base, 0, poll_interval};
    std::memcpy(reinterpret_cast<void *>(cursor), &packet, sizeof(packet));
    render::set_packet_cursor(buffer, render::packet_cursor(buffer) + sizeof(packet));
}

}  // namespace

extern "C" void bb_render_emit_shader_preparation_range_01475e10(PacketBuffer *buffer, uint32_t base, uint32_t size,
                                                                 uint32_t targets, uint32_t actions,
                                                                 uint32_t more_targets, uint32_t flag)
{
    emit(buffer, coherence_control(actions, targets | more_targets, flag), size, base);
}

extern "C" void bb_render_emit_shader_preparation_event_01477f40(PacketBuffer *buffer, uint32_t actions,
                                                                 uint32_t targets, uint32_t flag)
{
    emit(buffer, coherence_control(actions, targets, flag), whole_memory_size, 0);
}
