// SPDX-License-Identifier: GPL-2.0-or-later
// The last-record caller emits this three-DWORD packet from a captured value.
// Preserve callback/result widths and packet-store aliasing; no hardware meaning
// or extra callback/physical-frame contract is inferred from the packet constants.
#pragma once

#include "present_finish.h"

namespace render {
// Only AL admits capacity success, but an AL-zero path returns the callback's
// complete opaque RAX. This wire does not close upper bits of an AL-only source
// callback replacement or any additional inherited GP/SIMD/stack inputs.
using LastRecordPacketCapacity = uint64_t (*)(GpuCommandState *, uint32_t requested_words,
                                             uintptr_t argument);
struct LastRecordPacketBuffer {
    uint8_t unknown_0x00[0x08];
    PresentFinishField<uintptr_t> limit;
    PresentFinishField<uintptr_t> cursor;
    PresentFinishField<LastRecordPacketCapacity> capacity;
    PresentFinishField<uintptr_t> capacity_argument;
};
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
static_assert(offsetof(LastRecordPacketBuffer, limit) == 0x08);
static_assert(offsetof(LastRecordPacketBuffer, cursor) == 0x10);
static_assert(offsetof(LastRecordPacketBuffer, capacity) == 0x18);
static_assert(offsetof(LastRecordPacketBuffer, capacity_argument) == 0x20);
static_assert(sizeof(LastRecordPacketBuffer) == 0x28);

struct LastRecordPacket {
    PresentFinishField<uint32_t> header;
    PresentFinishField<uint32_t> zero_word;
    PresentFinishField<uint32_t> value;
};
static_assert(offsetof(LastRecordPacket, header) == 0x00);
static_assert(offsetof(LastRecordPacket, zero_word) == 0x04);
static_assert(offsetof(LastRecordPacket, value) == 0x08);
static_assert(sizeof(LastRecordPacket) == 0x0c);

RT_ORIGINAL(0x014769e0, emit_present_last_record_packet,
            uint64_t(GpuCommandState *, uint32_t value));

// Valid ordinary nonconcurrent backing memory and callable callback bindings are
// required. Callback failure retains its own side effects; success writes three
// separate DWORDs then publishes/returns the captured cursor plus twelve bytes.
// No new null/capacity guard, rollback, ambient-register, fault-progress or exact
// compiler-store-lowering equivalence is supplied by this source profile.
}  // namespace render
