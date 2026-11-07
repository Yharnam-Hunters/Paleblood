// SPDX-License-Identifier: GPL-2.0-or-later
// Buffer-pool geometry and marker operations used by presentation-capacity
// callbacks. Each block ends with a marker; the counter selects the first block
// inspected by an acquisition attempt.
#pragma once
#include "runtime/original.h"

#include "byte_field.h"

namespace render {
struct PresentBufferPoolWord {
    Field<uint64_t> value;
};
static_assert(offsetof(PresentBufferPoolWord, value) == 0x00);
static_assert(sizeof(PresentBufferPoolWord) == 0x08);

struct PresentBufferPool {
    uint8_t unknown_0x00[0x08];
    Field<uint64_t> slots;
    Field<uint64_t> block_count;
    Field<uint64_t> base;
    PresentBufferPoolWord counter;
};
static_assert(offsetof(PresentBufferPool, slots) == 0x08);
static_assert(offsetof(PresentBufferPool, block_count) == 0x10);
static_assert(offsetof(PresentBufferPool, base) == 0x18);
static_assert(offsetof(PresentBufferPool, counter) == 0x20);
static_assert(sizeof(PresentBufferPool) == 0x28);
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));

RT_ORIGINAL(0x02ad3d40, capture_present_buffer_pool_record,
            uint64_t(const PresentBufferPool *, uint64_t begin));
RT_ORIGINAL(0x02ad3b80, pop_present_buffer_pool,
            uint32_t(PresentBufferPool *, void *acquired_buffer));

namespace present_pool_calls {
// Advance the pool counter through its original helper. The marker's shared
// compare/exchange operation preserves the executable's marker contract.
RT_ORIGINAL(0x0247eb80, advance_present_pool_counter, uint64_t(PresentBufferPoolWord *));
RT_ORIGINAL(0x0247ebc0, compare_exchange_present_pool_marker,
            uint64_t(void *word, uint64_t desired, uint64_t expected));
}  // namespace present_pool_calls
}  // namespace render
