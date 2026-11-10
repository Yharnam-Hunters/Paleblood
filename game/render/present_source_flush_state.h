// SPDX-License-Identifier: GPL-2.0-or-later
// State views used to flush changed presentation slot values. The table layouts describe
// native address strides; the count byte is not clamped to the declared slot-array lengths.
#pragma once

#include <limits>

#include "present_source.h"
#include "command_packets.h"

namespace render {
struct PresentSlotBank {
    static constexpr auto slot_count = 9;
    uintptr_t slots[slot_count];
};
static_assert(sizeof(PresentSlotBank) == 0x48);

struct PresentSourcePreparation {
    uint8_t unknown_0x000[0x60];
    static constexpr auto bank_count = 2;
    PresentSlotBank banks[bank_count];
    uint8_t unknown_0x0f0[0x10];
    static constexpr auto pending_value_count = 8;
    uint64_t pending_values[pending_value_count];
    uint8_t unknown_0x140[0x6];
    uint8_t slot_count;
    uint8_t unknown_0x147;
    static constexpr auto cached_value_count = 8;
    uint64_t cached_values[cached_value_count];
    uint32_t flags;
};
static_assert(offsetof(PresentSourcePreparation, banks) == 0x60);
static_assert(offsetof(PresentSourcePreparation, pending_values) == 0x100);
static_assert(offsetof(PresentSourcePreparation, slot_count) == 0x146);
static_assert(offsetof(PresentSourcePreparation, cached_values) == 0x148);
static_assert(offsetof(PresentSourcePreparation, flags) == 0x188);

struct PresentSelectedSlot {
    uint8_t unknown_0x00[0x3c];
    uint32_t encoding;
};
static_assert(offsetof(PresentSelectedSlot, encoding) == 0x3c);
struct PresentSourceContext {
    uint8_t unknown_0x00[0x10];
    PacketBuffer commands;
};
static_assert(offsetof(PresentSourceContext, commands) == 0x10);

RT_ORIGINAL(0x02564ed0, render_present_encoding_units, uint64_t(uint32_t encoding));
RT_ORIGINAL(0x01473840, render_present_slot_emit,
            void(PacketBuffer *, uint32_t index, const uint64_t *value));
}  // namespace render
