// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel (Dantelion2 core): waiting on a condition variable.
//
// kernel_condition_wait (0x02483e80, PthreadCondition.cpp in the engine): waits on the condition
// with the mutex, forever when the timeout is -1, else with scePthreadCondTimedwait. Returns 0,
// or -3 when the timed wait timed out. A null mutex, EPERM, EINVAL and any other error call the
// engine's fatal error (0x024b55b0) with the file and line; that function does not return. The
// code after each call is kept as the original has it (a missing mutex still goes on to wait;
// after EPERM the unknown-error report follows; after EINVAL it returns -3). Ghidra treats the
// fatal error as non-returning and does not show those jumps (QUIRKS.md).
//
// The community frame-rate patches replace the timed-wait call with a return, which would
// return to a garbage address if it ran: every wait the game makes here is untimed in practice.
// BB_TARGET_FPS does not change this function.
#include <cstdint>
#include <cstdio>

#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t cond_wait_thunk = 0x02fc0d78;        // scePthreadCondWait
constexpr uint32_t cond_timedwait_thunk = 0x02fc0d68;   // scePthreadCondTimedwait
constexpr uint32_t fatal_error = 0x024b55b0;
constexpr uint32_t str_file = 0x04d3c0af;               // .../PthreadCondition.cpp
constexpr uint32_t str_invalid_parameter = 0x04d3c158, str_einval = 0x04d3c177, str_eperm = 0x04d3c1c1,
                   str_unknown = 0x04d3c2ae;
constexpr int32_t error_eperm = static_cast<int32_t>(0x80020001), error_einval = static_cast<int32_t>(0x80020016),
                  error_timedout = static_cast<int32_t>(0x8002003c);
constexpr uint32_t timed_out = 0xfffffffd;

using Wait = int32_t(void *cond, void *mutex);
using TimedWait = int32_t(void *cond, void *mutex, uint32_t usec);
using Fatal = int32_t(const char *file, int line, const char *message, ...);

int32_t fatal(int line, uint32_t message)
{
    return rt::fn<Fatal>(fatal_error)(rt::ptr<const char>(str_file), line, rt::ptr<const char>(message));
}

}  // namespace

extern "C" uint32_t bb_kernel_condition_wait(void *cond, void *mutex, int32_t timeout_us)
{
    if (!mutex)
        fatal(0x9a, str_invalid_parameter);
    const int32_t r = timeout_us == -1 ? rt::fn<Wait>(cond_wait_thunk)(cond, mutex)
                                       : rt::fn<TimedWait>(cond_timedwait_thunk)(cond, mutex, static_cast<uint32_t>(timeout_us));
    const int number = rt_capture_begin("kernel_condition_wait");
    if (number >= 0) {
        char json[1024];
        std::snprintf(json, sizeof json,
                      "{\"schema\": 1, \"address\": \"0x02483e80\", \"id\": \"capture_%04d\", \"returns\": \"i32\", "
                      "\"args\": {\"rdi\": \"buf:cond\", \"rsi\": \"%s\", \"rdx\": \"0x%x\"}, "
                      "\"buffers\": {\"cond\": {\"size\": 8}, \"mutex\": {\"size\": 8}}, \"memory\": [], \"stubs\": [], "
                      "\"imports\": [{\"name\": \"%s\", \"argc\": %d, \"ret\": %d}]}",
                      number, mutex ? "buf:mutex" : "0x0", static_cast<uint32_t>(timeout_us),
                      timeout_us == -1 ? "scePthreadCondWait" : "scePthreadCondTimedwait", timeout_us == -1 ? 2 : 3, r);
        rt_capture_write("kernel_condition_wait", number, json);
    }
    if (r >= 0) {
        if (r != 0)
            fatal(0xb5, str_unknown);
        return 0;
    }
    if (r == error_eperm) {
        fatal(0xb3, str_eperm);
        fatal(0xb5, str_unknown);
        return 0;
    }
    if (r == error_einval) {
        fatal(0xaf, str_einval);
        return timed_out;
    }
    if (r == error_timedout)
        return timed_out;
    fatal(0xb5, str_unknown);
    return 0;
}
