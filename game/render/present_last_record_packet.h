// SPDX-License-Identifier: GPL-2.0-or-later
// The last-record path writes DB_RENDER_CONTROL with a one-value SET_CONTEXT_REG packet.
// Capacity callbacks and packet output may change the shared command-buffer storage.
#pragma once

#include "command_capacity_result.h"

namespace render {
struct LastRecordPacket {
    Field<uint32_t> header;
    Field<uint32_t> register_index;
    Field<uint32_t> value;
};
static_assert(offsetof(LastRecordPacket, header) == 0x00);
static_assert(offsetof(LastRecordPacket, register_index) == 0x04);
static_assert(offsetof(LastRecordPacket, value) == 0x08);
static_assert(sizeof(LastRecordPacket) == 0x0c);

}  // namespace render
