// SPDX-License-Identifier: GPL-2.0-or-later
// State and original calls used to configure a presentation viewport and related registers.
#pragma once

#include "command_reservation.h"

#include "present_command_configuration.h"
#include "runtime/original.h"

namespace render {

struct RecordConfigurationScratch {
    RecordConfigurationRegister register_state;
    RecordConfigurationFloat3 translation;
    RecordConfigurationFloat3 scale;
};
static_assert(offsetof(RecordConfigurationScratch, register_state) == 0x00);
static_assert(offsetof(RecordConfigurationScratch, translation) == 0x08);
static_assert(offsetof(RecordConfigurationScratch, scale) == 0x14);
static_assert(sizeof(RecordConfigurationScratch) == 0x20);

// Viewport configuration forwards the incoming end_y DWORD alongside scale and
// translation. The lower viewport helper does not read that argument.
RT_ORIGINAL(0x014765e0, configure_viewport,
            void(PacketBuffer *, uint32_t zero, RecordConfigurationFloat3 *scale,
                 RecordConfigurationFloat3 *translation, uint32_t inherited_end_y,
                 float first, float second));
RT_ORIGINAL(0x01485210, initialize_pa_cl_vte_cntl,
            void(RecordConfigurationRegister *));
RT_ORIGINAL(0x01485220, set_pa_cl_vte_cntl,
            uint32_t(RecordConfigurationRegister *, uint32_t));
RT_ORIGINAL(0x01476980, emit_pa_cl_vte_cntl,
            void(PacketBuffer *, uint32_t));
RT_ORIGINAL(0x01476790, configure_screen_scissor,
            void(PacketBuffer *, uint32_t, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x01473650, set_screen_offset,
            void(PacketBuffer *, uint32_t, uint32_t));
RT_ORIGINAL(0x014736d0, set_guard_band_clip_adjust,
            void(PacketBuffer *, float, float));
RT_ORIGINAL(0x01473750, set_guard_band_discard_adjust,
            void(PacketBuffer *, float, float));

RT_GLOBAL(0x04cd168c, record_configuration_half, const float);
RT_GLOBAL(0x04cd1690, record_configuration_negative_half, const float);
RT_GLOBAL(0x04cd1694, record_configuration_one, const float);
RT_GLOBAL(0x04cd1698, record_configuration_sixteenth, const float);
RT_GLOBAL(0x04cd169c, guard_band_lower_margin, const float);
RT_GLOBAL(0x04cd16a0, guard_band_upper_limit, const float);
RT_GLOBAL(0x04cd16b0, record_configuration_magnitude_mask, const uint32_t);

inline int32_t record_configuration_signed_word(uint32_t word)
{
    int32_t value;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

inline float mask_record_configuration_float(float value, uint32_t mask)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    bits &= mask;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace render
