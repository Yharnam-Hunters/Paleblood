// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize a hardware-state command section in the current packet buffer.
// A successful capacity callback can replace the storage before initialization.
#pragma once
#include "runtime/original.h"

#include "command_capacity_result.h"

namespace render {

namespace context_section_calls {
// sceGnmDrawInitDefaultHardwareState200 receives the full available-word count
// and returns the number of words emitted as a DWORD.
RT_ORIGINAL(0x02fc04a8, draw_init_default_hardware_state_200,
            uint32_t(uintptr_t destination, uint64_t available_words));
}  // namespace context_section_calls

}  // namespace render
