// SPDX-License-Identifier: GPL-2.0-or-later
// Run the presentation context's finishing calls in order with a zeroed snapshot.
#include "present_context_finish.h"

extern "C" void bb_render_finish_present_context(render::PresentContextFinishView *context)
{
    using namespace render;
    using namespace context_finish_calls;
    ContextFinishScratch scratch{};
    prepare_context_finish(&context->preparation, context, &scratch);
    flush_shader_preparation(&context->banks, &context->commands.commands);
    finish_context_shader(&context->shader, context);
    // The command state starts at this prefix and overlaps the embedded shader
    // and preparation fields; it is a view of the same context storage.
    auto *commands = reinterpret_cast<CommandFinishStateView *>(&context->commands);
    finish_present_commands(commands);
    finish_context_pair_state(&context->pair_state);
    finish_context_scalar_state(&context->scalar_state);
    finish_context_preparation(&context->preparation);
}
