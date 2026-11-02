// SPDX-License-Identifier: GPL-2.0-or-later
// Prefixes touched by the presentation-record consumer. Helper-owned structures and
// register meanings remain opaque; field names retain their observed offsets.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {
struct RecordCommandBuffer { uint8_t unknown_0x00; };
struct RecordShaderState { uint8_t unknown_0x00; };
struct RecordPreparationState { uint8_t unknown_0x00; };
struct SharedPresentDescriptor;

struct PresentRecordView {
    static constexpr auto words_extent_0 = 5;
    uint64_t words[words_extent_0];
    uint32_t tail_28;
};
static_assert(offsetof(PresentRecordView, tail_28) == 0x28);
static_assert(sizeof(PresentRecordView) == 0x30);
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
    RecordCommandBuffer commands;
    uint8_t unknown_0x0011[0x3f];
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

RT_ORIGINAL(0x01486d60, record_classification_byte, uint8_t(PresentRecordView *));
RT_ORIGINAL(0x01486d50, record_secondary_byte, uint8_t(PresentRecordView *));
RT_ORIGINAL(0x02abaeb0, record_range_begin, uint32_t(uintptr_t resource));
RT_ORIGINAL(0x02abaed0, record_range_length, uint32_t(uintptr_t extent));
RT_ORIGINAL(0x01475da0, begin_record_commands, void(RecordCommandBuffer *, uint32_t mode));
RT_ORIGINAL(0x01486d70, record_first_parameter, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x014868d0, record_second_parameter, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x01496f60, configure_record_parameters,
            void(RecordCommandBuffer *, uint32_t zero_x, uint32_t zero_y,
                 uint32_t first, uint32_t second, float scalar, float zero));
RT_ORIGINAL(0x014868f0, record_option_value, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x01477150, configure_record_option,
            void(RecordCommandBuffer *, uint32_t enabled, uint32_t zero));
RT_ORIGINAL(0x01484ea0, initialize_record_register_a, void(RecordRegisterState *));
RT_ORIGINAL(0x014854b0, initialize_record_register_b, void(RecordRegisterState *));
RT_ORIGINAL(0x01485380, initialize_record_register_c, void(RecordRegisterState *));
RT_ORIGINAL(0x01485390, set_record_register_range,
            void(RecordRegisterState *, uint32_t first, uint32_t last));
RT_ORIGINAL(0x014853f0, set_record_register_enabled, void(RecordRegisterState *, uint32_t enabled));
RT_ORIGINAL(0x01473090, emit_record_register_a,
            void(RecordCommandBuffer *, uint32_t slot, uint32_t value));
RT_ORIGINAL(0x01472fd0, emit_record_mask, void(RecordCommandBuffer *, uint32_t mask));
RT_ORIGINAL(0x014733a0, emit_record_register_c, void(RecordCommandBuffer *, uint32_t value));
RT_ORIGINAL(0x01473400, emit_record_register_b, void(RecordCommandBuffer *, uint32_t value));
RT_ORIGINAL(0x02ad1e20, select_record_preparation,
            void(RecordPreparationState *, RecordCommandBuffer *, uint32_t slot));
RT_ORIGINAL(0x01473320, emit_record_state_mode,
            void(RecordCommandBuffer *, uint32_t mode, uint32_t value));
RT_ORIGINAL(0x02ace1e0, configure_record_shader,
            void(RecordShaderState *, RecordCommandBuffer *, SharedPresentDescriptor *, uint32_t zero));
RT_ORIGINAL(0x02ad1e10, bind_prepared_record,
            void(RecordPreparationState *, RecordCommandBuffer *, uint32_t slot, PresentRecordView *));
RT_ORIGINAL(0x01473fe0, emit_record_pipeline_mode, void(RecordCommandBuffer *, uint32_t mode));
RT_ORIGINAL(0x01475370, emit_record_enable, void(RecordCommandBuffer *, uint32_t enabled));
RT_ORIGINAL(0x02ace5e0, configure_record_shader_secondary,
            void(RecordShaderState *, RecordCommandBuffer *, SharedPresentDescriptor *, uint32_t zero));
RT_ORIGINAL(0x01486520, set_present_record_range,
            void(PresentRecordView *, uintptr_t first, uint32_t last));
RT_ORIGINAL(0x014741b0, emit_present_record_step, void(RecordCommandBuffer *, uint32_t mode));
RT_ORIGINAL(0x02acfc20, finish_record_shader, void(RecordShaderState *, RecordCommandBuffer *));
RT_ORIGINAL(0x02ad1e30, clear_prepared_record,
            void(RecordPreparationState *, RecordCommandBuffer *, uint32_t slot));
RT_ORIGINAL(0x02ad1e70, finish_record_preparation, void(RecordPreparationState *, RecordCommandBuffer *));

RT_GLOBAL(0x04c6fa40, record_scalar_parameter, float);
RT_GLOBAL(0x04c70350, record_cache_token_a, uint64_t);
RT_GLOBAL(0x04c70358, record_cache_token_b, uint64_t);
RT_GLOBAL(0x0586c530, shared_present_descriptor, SharedPresentDescriptor);
}  // namespace render
