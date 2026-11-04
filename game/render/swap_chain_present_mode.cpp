// SPDX-License-Identifier: GPL-2.0-or-later
// Swap-chain facade called by GXSwapChainCtrl: reject unsupported modes before reading the
// object, then retain that object and load its full borrowed source pointer. The incoming
// mode contributes only its low 32 bits to the lower call.
#include "swap_chain.h"

namespace {
constexpr uint32_t last_present_mode = 4;
constexpr int32_t invalid_present_mode = 8;
}  // namespace

extern "C" int32_t bb_render_swap_chain_present_mode(render::SwapChain *self, uintptr_t incoming_mode)
{
    const auto mode = static_cast<uint32_t>(incoming_mode);
    if (mode > last_present_mode) return invalid_present_mode;
    return render::swap_chain_submit(self, self->source, mode);
}
