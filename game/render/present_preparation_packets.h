// SPDX-License-Identifier: GPL-2.0-or-later
// Command packets emitted for the dirty preparation fields. Callback admission
// and complete return values stay separate from the packets' selectors.
#pragma once

#include "present_finish.h"

namespace render {
using PreparationPacketCapacity = uint64_t (*)(GpuCommandState *, uint32_t requested_words,
                                              uintptr_t argument);
struct PreparationPacketBuffer {
    uint8_t unknown_0x00[0x08];
    PresentFinishField<uintptr_t> limit;
    PresentFinishField<uintptr_t> cursor;
    PresentFinishField<PreparationPacketCapacity> capacity;
    PresentFinishField<uintptr_t> capacity_argument;
};
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
static_assert(offsetof(PreparationPacketBuffer, limit) == 0x08);
static_assert(offsetof(PreparationPacketBuffer, cursor) == 0x10);
static_assert(offsetof(PreparationPacketBuffer, capacity) == 0x18);
static_assert(offsetof(PreparationPacketBuffer, capacity_argument) == 0x20);
static_assert(sizeof(PreparationPacketBuffer) == 0x28);

struct PreparationCommandPacket {
    PresentFinishField<uint32_t> header;
    PresentFinishField<uint32_t> selector;
    PresentFinishField<uint32_t> payload;
};
static_assert(offsetof(PreparationCommandPacket, header) == 0x00);
static_assert(offsetof(PreparationCommandPacket, selector) == 0x04);
static_assert(offsetof(PreparationCommandPacket, payload) == 0x08);
static_assert(sizeof(PreparationCommandPacket) == 0x0c);

namespace preparation_packet {
constexpr uint32_t word_shift = 2;
constexpr uint32_t words = 3;
constexpr uint32_t last_insufficient_word = words - 1;
constexpr uint32_t header = 0xc0016900;
constexpr uint32_t scalar_selector = 0xb;
constexpr uint32_t byte_selector = 0xa;
}

RT_ORIGINAL(0x01473590, emit_present_preparation_scalar,
            uint64_t(GpuCommandState *, float));
RT_ORIGINAL(0x014735f0, emit_present_preparation_byte,
            uint64_t(GpuCommandState *, uint32_t));

// These source profiles require ordinary nonconcurrent backing memory and a
// callable capacity callback. Only AL admits success; AL-zero returns its full
// opaque RAX. Extra inherited callback inputs, exact native physical frames,
// fault progress and compiler store lowering remain unestablished. The scalar
// spill is represented by its captured bits, not by an exposed native stack slot.
}  // namespace render
