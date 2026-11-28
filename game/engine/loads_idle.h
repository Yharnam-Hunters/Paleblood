// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the check the load code asks before it starts work (replaced in loads_idle.cpp).
#pragma once

#include "runtime/original.h"

namespace engine {

// True while none of the three engine counters is above 0 (meaning not established).
RT_ORIGINAL(0x01ee9b40, loads_idle, bool());

}  // namespace engine
