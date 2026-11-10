// SPDX-License-Identifier: GPL-2.0-or-later
// Counter waits and submission of saved presentation command regions.
// Byte counts wrap at 32 bits and round down to whole DWORDs.
#pragma once

#include "byte_field.h"

#include "command_regions.h"
#include "command_counter_wait.h"
#include "present_command_finish.h"

namespace render {
namespace finish_snapshot_calls {
RT_ORIGINAL(0x01489a10, sceGnmSubmitCommandBuffers,
            void(uint32_t count, const uint64_t *begins,
                 const uint32_t *sizes, const uint64_t *ccb_begins, const uint32_t *ccb_sizes));
}  // namespace finish_snapshot_calls
}  // namespace render
