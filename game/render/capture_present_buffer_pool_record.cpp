// SPDX-License-Identifier: GPL-2.0-or-later
// Locate the containing pool record using arithmetic shift by three followed by unsigned 64-bit
// remainder.
// The wrapped negative-looking difference is not subjected to signed division.
#include "present_buffer_pool.h"
#include "../engine/integer.h"

namespace {
constexpr uint64_t pool_word_bytes = sizeof(uint64_t);
constexpr uint32_t pool_word_shift = 3;
uint64_t arithmetic_pool_word_shift(uint64_t difference)
{
    return static_cast<uint64_t>(static_cast<int64_t>(difference) >> pool_word_shift);
}
}  // namespace

extern "C" uint64_t bb_render_capture_present_buffer_pool_record(
    const render::PresentBufferPool *pool, uint64_t begin)
{
    if (begin == 0) return 0;
    const uint64_t base = pool->base.get();
    const uint64_t difference = begin - base;
    const uint64_t shifted = arithmetic_pool_word_shift(difference);
    const uint64_t slots = pool->slots.get();
    // Keep unsigned DIVQ after the arithmetic shift. A zero slot count still
    // executes the division rather than selecting a substitute record.
    const uint64_t remainder = engine::divide_u64(shifted, slots).remainder;
    const uint64_t whole = shifted - remainder;
    return base + whole * pool_word_bytes;
}
