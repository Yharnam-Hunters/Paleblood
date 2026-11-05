// SPDX-License-Identifier: GPL-2.0-or-later
// Counter waits share the same target, wrap handling and live pointer reloads.
#pragma once

#include "command_reservation.h"
#include "runtime/original.h"

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

namespace counter_wait_calls {
RT_ORIGINAL(0x01477b30, emit_counter_operation,
            void(PacketBuffer *, uintptr_t, uint32_t, uint32_t, uint32_t));
}  // namespace counter_wait_calls

inline void prepare_counter_wait(CounterWaitState *counters, PacketBuffer *commands)
{
    uint32_t producer;
    uint32_t observed;
    std::memcpy(&producer, counters->producer_count.get(), sizeof(producer));
    std::memcpy(&observed, counters->observed_count.get(), sizeof(observed));
    const uint32_t target = producer - 1;
    if (observed == target) return;
    constexpr uint32_t sign_bit = 0x80000000;
    constexpr uint32_t full_mask = UINT32_MAX;
    constexpr uint32_t initial_wait_operation = 1;
    constexpr uint32_t target_wait_operation = 5;
    if ((observed & sign_bit) != 0 && (target & sign_bit) == 0) {
        counter_wait_calls::emit_counter_operation(
            commands, counters->wait_label.get(), full_mask, initial_wait_operation, observed);
    }
    // The first operation may replace the label; either may replace the output.
    counter_wait_calls::emit_counter_operation(
        commands, counters->wait_label.get(), full_mask, target_wait_operation, target);
    std::memcpy(counters->observed_count.get(), &target, sizeof(target));
}
}  // namespace render
