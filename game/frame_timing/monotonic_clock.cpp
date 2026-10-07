// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the game's monotonic millisecond clock.
//
// frame_timing_get_monotonic_ms (0x0111a7f0) reads CLOCK_MONOTONIC and writes the milliseconds
// since its first call to *out. The seconds of that first call are kept in a shared clock state
// object (field +0x10), which the microsecond variant at 0x0111a760 uses too. All arithmetic is
// 32-bit and wraps, as in the original.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

struct Timespec {
    int64_t seconds;
    int64_t nanoseconds;
};

using ClockGettime = int(int clock_id, Timespec *ts);

// Library thunk of sceKernelClockGettime.
constexpr uint32_t clock_gettime_thunk = 0x02fbfe58;
// Global pointer to the clock state object.
constexpr uint32_t clock_state_pointer = 0x056d6ae8;
constexpr uint32_t base_seconds_offset = 0x10;
constexpr int clock_monotonic = 4;
// Returned unchanged when the clock call fails (*out is not written).
constexpr int clock_error = 0x21;

void capture(int number, int clock_result, const Timespec &ts, int32_t base_before)
{
    char json[1024];
    // The case scripts the clock call: it writes the whole Timespec (seconds, nanoseconds),
    // 16 bytes little-endian, through its second argument.
    unsigned char raw[sizeof(Timespec)];
    std::memcpy(raw, &ts, sizeof raw);
    char ts_hex[2 * sizeof raw + 1];
    for (size_t i = 0; i < sizeof raw; i++)
        std::snprintf(ts_hex + 2 * i, 3, "%02x", raw[i]);
    const uint32_t base = static_cast<uint32_t>(base_before);
    snprintf(json, sizeof json,
             "{\"schema\": 1, \"address\": \"0x0111a7f0\", \"id\": \"capture_%04d\", "
             "\"returns\": \"i32\", \"args\": {\"rdi\": \"buf:out\"}, "
             "\"buffers\": {\"out\": {\"size\": 4}, \"state\": {\"size\": 20, \"bytes\": "
             "\"00000000000000000000000000000000%02x%02x%02x%02x\"}}, "
             "\"memory\": [{\"addr\": \"0x%08x\", \"pointer\": \"state\"}], "
             "\"imports\": [{\"name\": \"sceKernelClockGettime\", \"argc\": 2, \"ret\": %d, "
             "\"writes\": [{\"arg\": 1, \"offset\": 0, \"size\": 16, \"bytes\": \"%s\"}]}]}",
             number, base & 0xff, (base >> 8) & 0xff, (base >> 16) & 0xff, base >> 24,
             clock_state_pointer, clock_result, ts_hex);
    rt_capture_write("frame_timing_get_monotonic_ms", number, json);
}

}  // namespace

extern "C" int bb_frame_timing_get_monotonic_ms(uint32_t *out)
{
    Timespec ts;
    const int result = rt::fn<ClockGettime>(clock_gettime_thunk)(clock_monotonic, &ts);
    const int number = rt_capture_begin("frame_timing_get_monotonic_ms");
    if (result != 0) {
        if (number >= 0)
            capture(number, result, ts, 0);
        return clock_error;
    }
    auto *state = *rt::ptr<unsigned char *>(clock_state_pointer);
    auto *base_seconds = reinterpret_cast<int32_t *>(state + base_seconds_offset);
    if (number >= 0)
        capture(number, result, ts, *base_seconds);

    const uint32_t seconds = static_cast<uint32_t>(ts.seconds);
    if (*base_seconds == 0)
        *base_seconds = static_cast<int32_t>(seconds);
    const uint32_t elapsed_ms = (seconds - static_cast<uint32_t>(*base_seconds)) * 1000u;
    *out = elapsed_ms + static_cast<uint32_t>(ts.nanoseconds) / 1000000u;
    return 0;
}
