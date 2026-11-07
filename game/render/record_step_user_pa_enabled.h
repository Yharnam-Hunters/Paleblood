// SPDX-License-Identifier: GPL-2.0-or-later
// Normalize the user-PA query result to the byte predicate used by record-step dispatch.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
// Record-step dispatch tests the low 32 bits of the original query result for zero.
RT_ORIGINAL(0x02fc07a8, native_is_user_pa_enabled, uint32_t());

// Return one when the query's low DWORD is nonzero, otherwise zero.
RT_ORIGINAL(0x01489c40, record_step_user_pa_enabled, uint8_t());
}  // namespace render
