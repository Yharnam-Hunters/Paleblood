// SPDX-License-Identifier: GPL-2.0-or-later
// Embedded command, shader and preparation states finished by the presentation
// context. The command-state view overlaps the following embedded state objects.
#pragma once

#include "command_reservation.h"

#include "runtime/original.h"

#include "present_command_finish.h"
#include "present_context_reset_state.h"
#include "present_context_shader_reset_state.h"
#include "shader_preparation_state.h"

namespace render {
struct PresentContextFinishView {
    uint8_t unknown_0000[0x10];
    CommandBufferSnapshot commands;
    ContextShaderResetState shader;
    uint8_t unknown_b2f0[0xb478 - 0xb2f0];
    ShaderPreparationState banks;
    uint8_t unknown_b49d[0xb518 - 0xb49d];
    ContextPairResetState pair_state;
    ContextScalarResetState scalar_state;
    ContextPreparationReset preparation;
};
static_assert(offsetof(PresentContextFinishView, commands) == 0x10);
static_assert(offsetof(PresentContextFinishView, shader) == 0x50);
static_assert(offsetof(PresentContextFinishView, banks) == 0xb478);
static_assert(offsetof(PresentContextFinishView, pair_state) == 0xb518);
static_assert(offsetof(PresentContextFinishView, scalar_state) == 0xb528);
static_assert(offsetof(PresentContextFinishView, preparation) == 0xb558);

struct ContextFinishScratch {
    static constexpr uint32_t scratch_bytes = 0x48;
    uint8_t payload[scratch_bytes];
};
static_assert(sizeof(ContextFinishScratch) == 0x48);

namespace context_finish_calls {
RT_ORIGINAL(0x02ad3070, prepare_context_finish,
            void(ContextPreparationReset *, PresentContextFinishView *, ContextFinishScratch *));
RT_ORIGINAL(0x02ab9850, flush_shader_preparation,
            void(ShaderPreparationState *, PacketBuffer *));
RT_ORIGINAL(0x02acd190, finish_context_shader,
            void(ContextShaderResetState *, PresentContextFinishView *));
RT_ORIGINAL(0x02aaf700, finish_present_commands, void(CommandFinishStateView *));
RT_ORIGINAL(0x02abba10, finish_context_pair_state, void(ContextPairResetState *));
RT_ORIGINAL(0x02ad3300, finish_context_scalar_state, void(ContextScalarResetState *));
RT_ORIGINAL(0x02ad2d20, finish_context_preparation, void(ContextPreparationReset *));
}  // namespace context_finish_calls
}  // namespace render
