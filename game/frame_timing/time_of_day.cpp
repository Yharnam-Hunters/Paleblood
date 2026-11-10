// SPDX-License-Identifier: GPL-2.0-or-later
// Three small readers of the time of day: plain microseconds, microseconds in an owner's own
// tick unit, and four seed words that mix the time with a counter.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "time_of_day.h"

namespace {

using namespace frame_timing::time_of_day;

// The owner of 0x01088550 (ten callers, 0x01099500..0x010d6c6a): only its tick length is known.
struct TickClockOwner {
    uint8_t unknown_0x000[0x1d8];
    uint64_t microseconds_per_tick;   // a divisor; zero would fault, as in the original
};
static_assert(offsetof(TickClockOwner, microseconds_per_tick) == 0x1d8);

// Four 32-bit words returned in rax:rdx: the time itself, then the time offset by a counter that
// advances by two on every call, so two calls in the same microsecond still differ.
struct TimeSeedWords {
    uint32_t seconds;
    uint32_t microseconds;
    uint32_t seconds_plus_counter;
    uint32_t microseconds_plus_counter_plus_one;
};
static_assert(sizeof(TimeSeedWords) == sizeof(unsigned __int128));

RT_GLOBAL(0x0570d83c, time_seed_counter, uint32_t);
constexpr uint32_t time_seed_counter_step = 2;

}  // namespace

// Microseconds since the epoch. Callers 0x02e62671 and 0x02e6a0eb.
extern "C" uint64_t bb_frame_timing_time_of_day_us()
{
    return now_microseconds();
}

// The time of day in the owner's ticks: microseconds divided (unsigned) by its tick length.
extern "C" uint64_t bb_frame_timing_time_of_day_ticks(const TickClockOwner *owner)
{
    return now_microseconds() / owner->microseconds_per_tick;
}

// The seed words: the counter is read once, and the stored value is that read plus two.
extern "C" unsigned __int128 bb_frame_timing_time_seed_words()
{
    Timeval time;
    c_gettimeofday(&time, nullptr);
    const uint32_t seconds = static_cast<uint32_t>(time.seconds);
    const uint32_t microseconds = static_cast<uint32_t>(time.microseconds);
    uint32_t &counter = time_seed_counter.get();
    const uint32_t previous = counter;
    counter = previous + time_seed_counter_step;
    const TimeSeedWords words{seconds, microseconds, previous + seconds, previous + microseconds + 1};
    return std::bit_cast<unsigned __int128>(words);
}
