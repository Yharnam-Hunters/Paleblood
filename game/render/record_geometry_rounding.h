// SPDX-License-Identifier: GPL-2.0-or-later
// Round descriptor counts to the power-of-two capacity required by flagged layouts.
#pragma once
#include "record_geometry_status.h"
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x0149e8d0, record_geometry_round_up_power_of_two, uint32_t(uint32_t value));
}  // namespace render
