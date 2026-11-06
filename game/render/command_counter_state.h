// SPDX-License-Identifier: GPL-2.0-or-later
// Live producer/observed counters and the label used by command submissions.
#pragma once

#include "byte_field.h"

namespace render {
struct CounterWaitState {
    uint8_t unknown_00[0x10];
    Field<uintptr_t> wait_label;
    Field<const uint32_t *> producer_count;
    Field<uint32_t *> observed_count;
};
static_assert(offsetof(CounterWaitState, wait_label) == 0x10);
static_assert(offsetof(CounterWaitState, producer_count) == 0x18);
static_assert(offsetof(CounterWaitState, observed_count) == 0x20);

}  // namespace render
