// SPDX-License-Identifier: GPL-2.0-or-later
// Pending presentation command lists, saved regions and owner/counter links
// used while finishing a presentation context.
#pragma once
#include "packet_buffer_bounds.h"

#include "command_reservation.h"
#include "command_counter_state.h"
#include "command_buffer_snapshot.h"
#include "command_submit.h"

#include "runtime/original.h"

namespace render {
using CommandFinishStateView = CommandSubmissionContext;

inline void copy_finish_snapshot_reverse(CommandBufferSnapshot &out,
                                          const CommandBufferSnapshot &in)
{
    auto *destination = reinterpret_cast<uint8_t *>(&out);
    const auto *source = reinterpret_cast<const uint8_t *>(&in);
    for (std::size_t offset = sizeof(CommandBufferSnapshot);
         offset > offsetof(PacketBuffer, cursor);) {
        offset -= sizeof(uint64_t);
        uint64_t value;
        std::memcpy(&value, source + offset, sizeof(value));
        std::memcpy(destination + offset, &value, sizeof(value));
    }
    const uintptr_t begin = packet_begin(&in.commands);
    const uintptr_t limit = packet_limit(&in.commands);
    set_packet_limit(&out.commands, limit);
    set_packet_begin(&out.commands, begin);
}

struct FinishBlockSlot { Field<uintptr_t> value; };
static_assert(sizeof(FinishBlockSlot) == 0x08);

RT_ORIGINAL(0x02ab0b00, submit_present_finish_snapshot,
            void(CommandFinishStateView *, CommandFinishStateView *optional,
                 CommandBufferSnapshot *, uint32_t flags));
}  // namespace render
