// SPDX-License-Identifier: GPL-2.0-or-later
// Write the GPU library's two VGT_INSTANCE_STEP_RATE registers in one packet.
// Publication adds to the live cursor, which the packet itself may overwrite.
#include "command_reservation.h"

namespace {
constexpr size_t instance_step_rate_count = 2;
struct InstanceStepRatePacket {
    render::Field<uint32_t> header;
    render::Field<uint32_t> register_index;
    render::Field<uint32_t> step_rates[instance_step_rate_count];
};
static_assert(offsetof(InstanceStepRatePacket, header) == 0x0);
static_assert(offsetof(InstanceStepRatePacket, register_index) == 0x4);
static_assert(offsetof(InstanceStepRatePacket, step_rates) == 0x8);
static_assert(sizeof(InstanceStepRatePacket) == 0x10);
}  // namespace

extern "C" void bb_render_set_instance_step_rates(render::PacketBuffer *commands,
                                                    uint32_t first, uint32_t second)
{
    constexpr uint32_t required_words = 4;
    constexpr uint32_t set_context_reg_two_values_header = 0xc0026900;
    constexpr uint32_t vgt_instance_step_rate_0_index = 0x2a8;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;

    auto *packet = reinterpret_cast<InstanceStepRatePacket *>(cursor);
    packet->header.set(set_context_reg_two_values_header);
    packet->register_index.set(vgt_instance_step_rate_0_index);
    packet->step_rates[0].set(first);
    packet->step_rates[1].set(second);
    render::set_packet_cursor(commands, render::packet_cursor(commands) + sizeof(InstanceStepRatePacket));
}
