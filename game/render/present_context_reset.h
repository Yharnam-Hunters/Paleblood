// SPDX-License-Identifier: GPL-2.0-or-later
// Original reset operations used by presentation-context finishing.
#pragma once

#include "present_context_reset_state.h"
#include "runtime/original.h"

namespace render {
RT_GLOBAL(0x04b37e64, context_reset_scalar_word, uint32_t);
}  // namespace render
