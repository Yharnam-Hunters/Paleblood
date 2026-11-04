// SPDX-License-Identifier: GPL-2.0-or-later
// The observed facade object prefix and its borrowed source pointer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {

struct SwapChain {
    uint8_t unknown_0x0000[0x33d0];
    uintptr_t source;
};
static_assert(offsetof(SwapChain, source) == 0x33d0);

// The facade retains its object in RDI, loads the full source pointer into RSI,
// and forwards the validated low 32-bit incoming mode separately in EDX.
RT_ORIGINAL(0x02ad6e50, swap_chain_submit,
            int32_t(SwapChain *, uintptr_t source, uint32_t mode));

}  // namespace render
