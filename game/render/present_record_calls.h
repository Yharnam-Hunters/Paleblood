// SPDX-License-Identifier: GPL-2.0-or-later
// Original call profiles used by the ordinary and final record consumers.
// Keep them separate from shared object views and inline short source forms.
#pragma once

#include "present_record_consumer.h"

namespace render::record_calls {
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
            void(RecordShaderState *, RecordCommandBuffer *, SharedPresentDescriptor *, SharedPresentDescriptor *optional_descriptor));
RT_ORIGINAL(0x02ad1e10, bind_prepared_record,
            void(RecordPreparationState *, RecordCommandBuffer *, uint32_t slot, PresentRecordView *));
RT_ORIGINAL(0x01473fe0, emit_record_pipeline_mode, void(RecordCommandBuffer *, uint32_t mode));
RT_ORIGINAL(0x01475370, emit_record_enable, void(RecordCommandBuffer *, uint32_t enabled));
RT_ORIGINAL(0x02ace5e0, configure_record_shader_secondary,
            void(RecordShaderState *, RecordCommandBuffer *, SharedPresentDescriptor *, SharedPresentDescriptor *optional_descriptor));
RT_ORIGINAL(0x01486520, set_present_record_range,
            void(PresentRecordView *, uintptr_t first, uint32_t last));
RT_ORIGINAL(0x014741b0, emit_present_record_step, void(RecordCommandBuffer *, uint32_t mode));
RT_ORIGINAL(0x02acfc20, finish_record_shader, void(RecordShaderState *, RecordCommandBuffer *));
RT_ORIGINAL(0x02ad1e30, clear_prepared_record,
            void(RecordPreparationState *, RecordCommandBuffer *, uint32_t slot));
RT_ORIGINAL(0x02ad1e70, finish_record_preparation, void(RecordPreparationState *, RecordCommandBuffer *));

}  // namespace render::record_calls
