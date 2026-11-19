// SPDX-License-Identifier: GPL-2.0-or-later
// Accessed prefix of the FMOD timing state, inferred from its clock consumers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// This is an accessed storage view, not a recovered original class declaration.
// The intervening accumulation/output fields are preserved by initialization.
struct FmodTimingStorage {
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t exclusion_start_tick;
    uint32_t exclusion_end_tick;
    uint32_t previous_sample_tick;
    uint32_t sample_tick;
    std::byte unknown_0x0018[0x10];
    uint8_t excluding;
    std::byte unknown_0x0029[3];
    uint32_t excluded_ticks;
    uint32_t exclusion_depth;
    uint8_t running;
    std::byte unknown_0x0035[3];
};
static_assert(offsetof(FmodTimingStorage, start_tick) == 0x00);
static_assert(offsetof(FmodTimingStorage, end_tick) == 0x04);
static_assert(offsetof(FmodTimingStorage, exclusion_start_tick) == 0x08);
static_assert(offsetof(FmodTimingStorage, exclusion_end_tick) == 0x0c);
static_assert(offsetof(FmodTimingStorage, previous_sample_tick) == 0x10);
static_assert(offsetof(FmodTimingStorage, sample_tick) == 0x14);
static_assert(offsetof(FmodTimingStorage, excluding) == 0x28);
static_assert(offsetof(FmodTimingStorage, excluded_ticks) == 0x2c);
static_assert(offsetof(FmodTimingStorage, exclusion_depth) == 0x30);
static_assert(offsetof(FmodTimingStorage, running) == 0x34);
static_assert(sizeof(FmodTimingStorage) == 0x38);

}  // namespace audio
