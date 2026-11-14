// SPDX-License-Identifier: GPL-2.0-or-later
// Set the ESGS ring item size and observe the final published register packet.
#include "packet_observation.h"
#include "present_register_packets.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t vgt_esgs_ring_itemsize_index = 0x2ab;
constexpr uint32_t itemsize_mask = 0x7fff;
constexpr uint32_t register_packet_words =
    sizeof(render::PreparationCommandPacket) / render::packet_word_bytes;

void observe_esgs_itemsize(const render::PacketObservation &call, uintptr_t cursor)
{
    if (!call.available) return;
    const auto *packet = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
    if (packet->header.get() != render::preparation_packet::set_context_reg_one_value_header ||
        packet->register_index.get() != vgt_esgs_ring_itemsize_index) {
        std::fputs("ESGS-itemsize observer: final packet has an unsupported header or register index\n", stderr);
        std::abort();
    }
    rt_observe_register(call.buffer, call.offset,
                        render::context_register_base + packet->register_index.get(), packet->payload.get());
}
}  // namespace

extern "C" void bb_render_set_esgs_ring_itemsize(render::PacketBuffer *commands, uint32_t itemsize)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, register_packet_words, cursor)) return;

    const auto call = render::observe_packet(commands, cursor);
    render::write_present_register_packet(commands, cursor, vgt_esgs_ring_itemsize_index, itemsize & itemsize_mask);
    observe_esgs_itemsize(call, cursor);
}
