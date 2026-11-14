// SPDX-License-Identifier: GPL-2.0-or-later
// Reset the two state fields to zero and the packed invalid-value sentinel.
#include "present_context_reset.h"

namespace {
constexpr uint64_t state_b518_sentinel = 0xffffffff00000000;
}

extern "C" void bb_render_reset_present_context_state_b518(render::ContextPairResetState *state)
{
    state->word_00.set(0);
    state->word_08.set(state_b518_sentinel);
}
