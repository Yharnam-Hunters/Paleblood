// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: seeding a thread's four-word random generator, which thread_random.cpp calls
// the first time a thread asks for its random state (and about a dozen other callers with
// generators of their own).
// - frame_timing_random_generator_seed_024f8110: a 64-bit linear congruential generator
//   (state times 0x5deece66d plus 11, modulo 2^64) starts from the 32-bit seed run through that
//   step once (its constructor, 0x02a86010), and the high 32 bits of that state and of the next
//   three fill the four words. The generator is dropped after one more step (its destructor does
//   nothing).
#include <cstddef>
#include <cstdint>

namespace {

constexpr uint64_t lcg_multiplier = 0x5deece66d;   // the rand48 multiplier
constexpr uint64_t lcg_increment = 0xb;
constexpr int high_word_shift = 32;
constexpr size_t generator_words = 4;

// The original's generator object: its constructor and the step use the same two constants
// (the constructor as immediates, the step from two read-only words at 0x04c6d380).
struct LinearCongruential {
    uint64_t state;

    explicit LinearCongruential(uint32_t seed) : state(step(seed)) {}
    static uint64_t step(uint64_t value) { return value * lcg_multiplier + lcg_increment; }
    uint32_t high_word() const { return static_cast<uint32_t>(state >> high_word_shift); }
    void advance() { state = step(state); }
};

}  // namespace

extern "C" void bb_frame_timing_random_generator_seed_024f8110(uint32_t *generator, uint32_t seed)
{
    LinearCongruential lcg(seed);
    for (size_t word = 0; word < generator_words; ++word) {
        generator[word] = lcg.high_word();
        lcg.advance();
    }
}
