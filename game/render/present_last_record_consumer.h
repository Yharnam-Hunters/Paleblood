// SPDX-License-Identifier: GPL-2.0-or-later
// Last-record presentation path: its range bank and 52-byte record differ from
// the ordinary consumer. Helpers still own command and shader representations.
#pragma once

#include "command_reservation.h"

#include "present_record_consumer.h"

namespace render {
struct LastPresentRecordView {
    static constexpr auto word_count = 6;
    Field<uint64_t> words[word_count];
    Field<uint32_t> parameters_30;
};
static_assert(offsetof(LastPresentRecordView, parameters_30) == 0x30);
static_assert(sizeof(LastPresentRecordView) == 0x34);

struct LastPresentRecordPayload {
    uint8_t unknown_0x00[0x8];
    Field<uintptr_t> resource;
    uint8_t unknown_0x10[0x4];
    Field<uint32_t> extent_14;
    uint8_t unknown_0x18[0xd];
    Field<uint8_t> flags_25;
};
static_assert(offsetof(LastPresentRecordPayload, resource) == 0x8);
static_assert(offsetof(LastPresentRecordPayload, extent_14) == 0x14);
static_assert(offsetof(LastPresentRecordPayload, flags_25) == 0x25);

struct LastPresentRecordContext {
    uint8_t unknown_0x0000[0x10];
    PacketBuffer commands;
    uint8_t unknown_0x0038[0x18];
    RecordShaderState shader_state;
    uint8_t unknown_0x0051[0xb427];
    Field<uint8_t> flags_b478;
    uint8_t unknown_0xb479[0x13];
    Field<uint32_t> minimum_b48c;
    Field<uint32_t> maximum_b490;
    uint8_t unknown_0xb494;
    Field<uint8_t> marker_b495;
    uint8_t unknown_0xb496[0x2];
    Field<uint32_t> flags_b498;
    uint8_t unknown_0xb49c[0x7c];
    Field<uint64_t> pending_b518;
    Field<uint32_t> mode_b520;
    Field<uint32_t> sentinel_b524;
    uint8_t unknown_0xb528[0x18];
    Field<uint32_t> sentinel_b540;
    uint8_t unknown_0xb544[0x4];
    Field<uint64_t> sentinel_b548;
    Field<uint8_t> flags_b550;
    uint8_t unknown_0xb551[0x7];
    RecordPreparationState preparation;
    uint8_t unknown_0xb559[0x47];
    Field<uint32_t> register_b5a0;
    Field<uint32_t> register_b5a4;
    uint8_t unknown_0xb5a8[0x148];
    Field<uint64_t> token_b6f0;
};
static_assert(offsetof(LastPresentRecordContext, commands) == 0x10);
static_assert(offsetof(LastPresentRecordContext, shader_state) == 0x50);
static_assert(offsetof(LastPresentRecordContext, flags_b478) == 0xb478);
static_assert(offsetof(LastPresentRecordContext, minimum_b48c) == 0xb48c);
static_assert(offsetof(LastPresentRecordContext, maximum_b490) == 0xb490);
static_assert(offsetof(LastPresentRecordContext, marker_b495) == 0xb495);
static_assert(offsetof(LastPresentRecordContext, flags_b498) == 0xb498);
static_assert(offsetof(LastPresentRecordContext, pending_b518) == 0xb518);
static_assert(offsetof(LastPresentRecordContext, mode_b520) == 0xb520);
static_assert(offsetof(LastPresentRecordContext, sentinel_b524) == 0xb524);
static_assert(offsetof(LastPresentRecordContext, sentinel_b540) == 0xb540);
static_assert(offsetof(LastPresentRecordContext, sentinel_b548) == 0xb548);
static_assert(offsetof(LastPresentRecordContext, flags_b550) == 0xb550);
static_assert(offsetof(LastPresentRecordContext, preparation) == 0xb558);
static_assert(offsetof(LastPresentRecordContext, register_b5a0) == 0xb5a0);
static_assert(offsetof(LastPresentRecordContext, register_b5a4) == 0xb5a4);
static_assert(offsetof(LastPresentRecordContext, token_b6f0) == 0xb6f0);

namespace last_record_calls {
RT_ORIGINAL(0x01472730, classify_record, uint8_t(LastPresentRecordView *));
RT_ORIGINAL(0x014726c0, record_option, uint32_t(LastPresentRecordView *));
RT_ORIGINAL(0x02ad2c30, reset_preparation,
            void(RecordPreparationState *, LastPresentRecordContext *));
RT_ORIGINAL(0x01485020, initialize_mode, void(RecordRegisterState *));
RT_ORIGINAL(0x01485080, set_first_mode, void(RecordRegisterState *, uint32_t));
RT_ORIGINAL(0x01485070, set_second_mode, void(RecordRegisterState *, uint32_t));
RT_ORIGINAL(0x014769e0, emit_mode, void(RecordCommandBuffer *, uint32_t));
RT_ORIGINAL(0x014721d0, set_record_index,
            void(LastPresentRecordView *, uint32_t first, uint32_t last));
RT_GLOBAL(0x04c6fa4c, scalar_parameter, float);
}  // namespace last_record_calls
}  // namespace render
