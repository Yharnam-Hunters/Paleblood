// SPDX-License-Identifier: GPL-2.0-or-later
// Original call profiles used by the ordinary and final record consumers.
// Keep them separate from shared object views and inline short source forms.
#pragma once

#include "command_packets.h"
#include "present_record_consumer.h"
#include "present_record_range_call.h"

namespace render {
struct TargetRestoreState;
struct ShaderPrimaryStateView;
struct ShaderPrimaryDescriptorView;
struct ShaderSecondaryState;
struct ShaderSecondaryDescriptorView;
}
namespace render::record_calls {
RT_ORIGINAL(0x01486d60, record_classification_byte, uint8_t(PresentRecordView *));
RT_ORIGINAL(0x01486d50, record_secondary_byte, uint8_t(PresentRecordView *));
RT_ORIGINAL(0x02abaeb0, record_range_begin, uint32_t(uintptr_t resource));
RT_ORIGINAL(0x02abaed0, record_range_length, uint32_t(uintptr_t extent));
RT_ORIGINAL(0x01475da0, emit_event_write, void(PacketBuffer *, uint32_t mode));
RT_ORIGINAL(0x01486d70, record_first_parameter, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x014868d0, record_second_parameter, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x01496f60, configure_record_parameters,
            void(PacketBuffer *, uint32_t zero_x, uint32_t zero_y,
                 uint32_t first, uint32_t second, float scalar, float zero));
RT_ORIGINAL(0x014868f0, record_option_value, uint32_t(PresentRecordView *));
RT_ORIGINAL(0x01477150, configure_record_option,
            void(PacketBuffer *, uint32_t enabled, uint32_t zero));
RT_ORIGINAL(0x01484ea0, initialize_record_register_a, void(RecordRegisterState *));
RT_ORIGINAL(0x014854b0, initialize_record_register_b, void(RecordRegisterState *));
RT_ORIGINAL(0x01485380, initialize_record_register_c, void(RecordRegisterState *));
RT_ORIGINAL(0x01485390, set_record_register_range,
            void(RecordRegisterState *, uint32_t first, uint32_t last));
RT_ORIGINAL(0x014853f0, set_record_register_enabled, void(RecordRegisterState *, uint32_t enabled));
RT_ORIGINAL(0x01473090, emit_record_register_a,
            void(PacketBuffer *, uint32_t slot, uint32_t value));
RT_ORIGINAL(0x01472fd0, set_cb_target_mask, void(PacketBuffer *, uint32_t mask));
RT_ORIGINAL(0x014733a0, set_db_depth_control, void(PacketBuffer *, uint32_t value));
RT_ORIGINAL(0x01473400, set_db_stencil_control, void(PacketBuffer *, uint32_t value));
RT_ORIGINAL(0x02ad1e20, select_preparation,
            void(RecordPreparationState *, PacketBuffer *, const void *record));
RT_ORIGINAL(0x01473320, set_cb_color_control,
            void(PacketBuffer *, uint32_t mode, uint32_t value));
RT_ORIGINAL(0x02ace1e0, configure_record_shader,
            void(ShaderPrimaryStateView *, PacketBuffer *, const ShaderPrimaryDescriptorView *,
                 const ShaderPrimaryDescriptorView *optional_descriptor));
RT_ORIGINAL(0x02ad1e10, bind_prepared_record,
            void(RecordPreparationState *, PacketBuffer *, uint32_t slot, PresentRecordView *));
RT_ORIGINAL(0x01473fe0, set_vgt_primitive_type, void(PacketBuffer *, uint32_t mode));
RT_ORIGINAL(0x01475370, emit_num_instances, void(PacketBuffer *, uint32_t enabled));
RT_ORIGINAL(0x02ace5e0, configure_record_shader_secondary,
            void(ShaderSecondaryState *, PacketBuffer *, const ShaderSecondaryDescriptorView *,
                 const ShaderSecondaryDescriptorView *optional_descriptor));
RT_ORIGINAL(0x014741b0, emit_draw_index_auto, void(PacketBuffer *, uint32_t mode));
RT_ORIGINAL(0x02acfc20, finish_record_shader, void(RecordShaderState *, PacketBuffer *));
RT_ORIGINAL(0x02ad1e30, restore_color_target_from_bank,
            void(TargetRestoreState *, PacketBuffer *, uint32_t slot));
RT_ORIGINAL(0x02ad1e70, restore_depth_target_from_bank, void(TargetRestoreState *, PacketBuffer *));

}  // namespace render::record_calls
