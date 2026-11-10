// SPDX-License-Identifier: GPL-2.0-or-later
// Each thread's random number state, seeded from the time of day the first time a thread asks
// for it. About sixty callers (0x02c4f807..0x02c6e50c) fetch the state through this function.
#include <cstddef>
#include <cstdint>

#include "../engine/thread.h"
#include "time_of_day.h"

namespace {

using namespace frame_timing::time_of_day;

constexpr size_t generator_words = 4;
struct ThreadRandom {
    uint32_t seed;           // the low 32 bits of the time of day in microseconds
    uint32_t generator[generator_words];
    uint8_t flags;           // thread_random_seeded once the generator is seeded
};
static_assert(offsetof(ThreadRandom, generator) == 0x4 && offsetof(ThreadRandom, flags) == 0x14);
constexpr uint8_t thread_random_seeded = 0x01;

// Where the state lives, from the thread pointer (a thread-local variable).
RT_GLOBAL(0x057e44a0, thread_random_offset, const int64_t);
// Seeds the four-word generator. It is passed the seed but reads its own entropy (0x02a86010).
RT_ORIGINAL(0x024f8110, random_generator_seed, void(void *generator, uint32_t seed));

ThreadRandom *this_thread_random()
{
    return reinterpret_cast<ThreadRandom *>(engine::thread_pointer() + thread_random_offset.get());
}

}  // namespace

extern "C" ThreadRandom *bb_frame_timing_thread_random()
{
    ThreadRandom *random = this_thread_random();
    if (!(random->flags & thread_random_seeded)) {
        // 32-bit arithmetic: seconds times a million plus microseconds, truncated.
        Timeval time;
        c_gettimeofday(&time, nullptr);
        const uint32_t seed = static_cast<uint32_t>(time.seconds) * static_cast<uint32_t>(microseconds_per_second) +
                              static_cast<uint32_t>(time.microseconds);
        random->seed = seed;
        random_generator_seed(random->generator, seed);
        random->flags |= thread_random_seeded;
    }
    // The original computes the thread-local address again for its result.
    return this_thread_random();
}
