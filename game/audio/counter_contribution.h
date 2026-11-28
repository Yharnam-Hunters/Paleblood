// SPDX-License-Identifier: GPL-2.0-or-later
// Counter contributions used by the FMOD output owner and its threads.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace audio {
struct CounterContext;

RT_ORIGINAL(0x011abd10, add_counter_contribution,
            void(CounterContext *counters, uint8_t bank, uint32_t classification, uint32_t count));
}  // namespace audio
