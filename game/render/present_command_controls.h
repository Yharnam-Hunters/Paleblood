// SPDX-License-Identifier: GPL-2.0-or-later
// Draw, event and instance-count command profiles used by presentation preparation.
// The consumer ignores the results; ordered packet writes and cursor publication remain
// significant.
#pragma once

#include "command_reservation.h"

#include "present_dirty_preparation_packets.h"
#include "runtime/original.h"

namespace render {
// The original indexes this mapped storage with the entire uint32 mode. No array
// extent or bounds check is established; do not substitute a shortened copied table.
RT_GLOBAL(0x04b2bfa0, record_control_mode_words, const uint32_t[]);

struct RecordControlPair {
    Field<uint32_t> header;
    Field<uint32_t> value;
};
static_assert(offsetof(RecordControlPair, header) == 0x00);
static_assert(offsetof(RecordControlPair, value) == 0x04);
static_assert(sizeof(RecordControlPair) == 0x08);

struct RecordControlTriple {
    Field<uint32_t> header;
    Field<uint32_t> first;
    Field<uint32_t> second;
};
static_assert(offsetof(RecordControlTriple, header) == 0x00);
static_assert(offsetof(RecordControlTriple, first) == 0x04);
static_assert(offsetof(RecordControlTriple, second) == 0x08);
static_assert(sizeof(RecordControlTriple) == 0x0c);

inline void write_record_control_pair(uintptr_t cursor, uint32_t header, uint32_t value)
{
    auto *packet = reinterpret_cast<RecordControlPair *>(cursor);
    packet->header.set(header);
    packet->value.set(value);
}

inline void advance_record_control_cursor(PacketBuffer *commands, uintptr_t bytes)
{
    // Some packets can overwrite this field. Their native ADD uses its value after
    // those stores, unlike the paths that publish from a previously captured cursor.
    set_packet_cursor(commands, packet_cursor(commands) + bytes);
}
}  // namespace render
