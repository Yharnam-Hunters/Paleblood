// SPDX-License-Identifier: GPL-2.0-or-later
// Finishing calls made by the owner-thread presentation path. Return values are
// ignored by that caller; native register behavior needs each complete body contract.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "runtime/original.h"

namespace render {
struct PresentGpuContext;
struct GpuCommandState;
RT_ORIGINAL(0x02568530, finish_present_context, void(PresentGpuContext *));
RT_ORIGINAL(0x02aaf700, finish_present_commands, void(GpuCommandState *));
RT_ORIGINAL(0x02fbe4f8, fail_present_finish_guard, void());
RT_GLOBAL(0x057e3be0, present_finish_guard_pointer, const uint64_t *);

template <typename T> struct PresentFinishField {
    uint8_t bytes[sizeof(T)];
    T get() const { T value; std::memcpy(&value, bytes, sizeof(value)); return value; }
    void set(T value) { std::memcpy(bytes, &value, sizeof(value)); }
};

inline uint64_t read_present_finish_guard(const uint64_t *pointer)
{
    uint64_t value;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}

inline void check_present_finish_guard_values(uint64_t current, uint64_t expected,
                                              const char *function)
{
    if (current == expected) return;
    fail_present_finish_guard();
    // The original bodies close after this import and a NOP. No returning-failure
    // continuation is established, so make that unsupported boundary explicit.
    std::fprintf(stderr, "%s: __stack_chk_fail returned; native fallthrough is unclosed\n",
                 function);
    std::abort();
}
}  // namespace render
