// SPDX-License-Identifier: GPL-2.0-or-later
// Shared presentation record views and constants. Call-owner bindings live in
// present_record_calls.h; short source helpers include only the views they use.
#pragma once

#include <cstddef>
#include <cstdint>

#include "command_packets.h"
#include "record_constructor_layout.h"
#include "runtime/original.h"

namespace render {
using RecordCommandBuffer = PacketBuffer;
struct RecordShaderState { uint8_t unknown_0x00; };
struct RecordPreparationState { uint8_t unknown_0x00; };
struct SharedPresentDescriptor;

// Consumers copy 44 bytes into a 48-byte local while retaining its native alignment.
struct alignas(8) PresentRecordView {
    RecordConstructorPrefix fields;
    uint8_t unknown_0x2c[4];
};
static_assert(offsetof(PresentRecordView, fields) == 0);
static_assert(offsetof(PresentRecordView, unknown_0x2c) == 0x2c);
static_assert(sizeof(PresentRecordView) == 0x30);
static_assert(alignof(PresentRecordView) == 0x8);
constexpr std::size_t present_record_prefix_bytes = 0x2c;

struct PresentRecordPayload {
    uint8_t unknown_0x00[0x8];
    uintptr_t resource;
    uint8_t unknown_0x10[0x4];
    uint32_t extent_14;
    uint8_t unknown_0x18[0xd];
    uint8_t flags_25;
};
static_assert(offsetof(PresentRecordPayload, resource) == 0x8);
static_assert(offsetof(PresentRecordPayload, extent_14) == 0x14);
static_assert(offsetof(PresentRecordPayload, flags_25) == 0x25);

struct PresentRecordContext {
    uint8_t unknown_0x0000[0x10];
    PacketBuffer commands;
    uint8_t unknown_0x0038[0x18];
    RecordShaderState shader_state;
    uint8_t unknown_0x0051[0xb427];
    uint8_t flags_b478;
    uint8_t unknown_0xb479[0xb];
    uint32_t minimum_b484;
    uint32_t maximum_b488;
    uint8_t unknown_0xb48c[0x8];
    uint8_t marker_b494;
    uint8_t unknown_0xb495[0x3];
    uint32_t flags_b498;
    uint8_t unknown_0xb49c[0x7c];
    uint64_t pending_b518;
    uint32_t mode_b520;
    uint32_t sentinel_b524;
    uint8_t unknown_0xb528[0x18];
    uint32_t sentinel_b540;
    uint8_t unknown_0xb544[0x4];
    uint64_t sentinel_b548;
    uint8_t flags_b550;
    uint8_t unknown_0xb551[0x7];
    RecordPreparationState preparation;
    uint8_t unknown_0xb559[0x7];
    uint64_t mask_b560;
    uint32_t register_b568;
    uint8_t unknown_0xb56c[0x34];
    uint32_t register_b5a0;
    uint32_t register_b5a4;
    uint8_t unknown_0xb5a8[0x140];
    uint64_t token_b6e8;
    uint64_t token_b6f0;
};
static_assert(offsetof(PresentRecordContext, commands) == 0x10);
static_assert(offsetof(PresentRecordContext, shader_state) == 0x50);
static_assert(offsetof(PresentRecordContext, flags_b478) == 0xb478);
static_assert(offsetof(PresentRecordContext, minimum_b484) == 0xb484);
static_assert(offsetof(PresentRecordContext, maximum_b488) == 0xb488);
static_assert(offsetof(PresentRecordContext, marker_b494) == 0xb494);
static_assert(offsetof(PresentRecordContext, flags_b498) == 0xb498);
static_assert(offsetof(PresentRecordContext, pending_b518) == 0xb518);
static_assert(offsetof(PresentRecordContext, mode_b520) == 0xb520);
static_assert(offsetof(PresentRecordContext, sentinel_b524) == 0xb524);
static_assert(offsetof(PresentRecordContext, sentinel_b540) == 0xb540);
static_assert(offsetof(PresentRecordContext, sentinel_b548) == 0xb548);
static_assert(offsetof(PresentRecordContext, flags_b550) == 0xb550);
static_assert(offsetof(PresentRecordContext, preparation) == 0xb558);
static_assert(offsetof(PresentRecordContext, mask_b560) == 0xb560);
static_assert(offsetof(PresentRecordContext, register_b568) == 0xb568);
static_assert(offsetof(PresentRecordContext, register_b5a0) == 0xb5a0);
static_assert(offsetof(PresentRecordContext, register_b5a4) == 0xb5a4);
static_assert(offsetof(PresentRecordContext, token_b6e8) == 0xb6e8);
static_assert(offsetof(PresentRecordContext, token_b6f0) == 0xb6f0);

struct alignas(8) RecordRegisterState {
    uint32_t word;
    uint8_t unknown_0x04[0x4];
};
static_assert(sizeof(RecordRegisterState) == 0x8);

RT_GLOBAL(0x04c6fa40, record_scalar_parameter, float);
RT_GLOBAL(0x04c70350, record_cache_token_a, uint64_t);
RT_GLOBAL(0x04c70358, record_cache_token_b, uint64_t);
RT_GLOBAL(0x0586c530, shared_present_descriptor, SharedPresentDescriptor);
}  // namespace render
