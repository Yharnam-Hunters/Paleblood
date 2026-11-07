// SPDX-License-Identifier: GPL-2.0-or-later
// Six-DWORD release packets and their argument-shuffling delegate. Returns preserve
// the full selected write address, or the opaque callback RAX on AL-zero failure.
#pragma once
#include "runtime/original.h"

#include "command_capacity_result.h"

namespace render {
struct PresentReleasePacket {
    Field<uint32_t> header;
    Field<uint32_t> control;
    Field<uint32_t> address_low;
    Field<uint32_t> address_high;
    Field<uint32_t> data_low;
    Field<uint32_t> data_high;
};
static_assert(offsetof(PresentReleasePacket, header) == 0);
static_assert(offsetof(PresentReleasePacket, control) == 0x4);
static_assert(offsetof(PresentReleasePacket, address_low) == 0x8);
static_assert(offsetof(PresentReleasePacket, address_high) == 0xc);
static_assert(offsetof(PresentReleasePacket, data_low) == 0x10);
static_assert(offsetof(PresentReleasePacket, data_high) == 0x14);
static_assert(sizeof(PresentReleasePacket) == 0x18);

RT_ORIGINAL(0x014778f0, emit_present_release_packet,
            uintptr_t(PacketBuffer *, uint32_t selector, uint32_t flags,
                      uint64_t address, uint32_t mode, uint64_t data,
                      uint32_t stack_selector, uint32_t stack_mode));
RT_ORIGINAL(0x02abb6d0, emit_present_release_delegate,
            uintptr_t(PacketBuffer *, uint32_t selector, uint64_t address,
                      uint64_t data, uint32_t stack_selector));
}  // namespace render
