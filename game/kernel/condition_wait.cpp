// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel (Dantelion2 core): waiting on a condition variable.
//
// kernel_condition_wait (0x02483e80, PthreadCondition.cpp in the engine): waits on the condition
// with the mutex, forever when the timeout is -1, else with scePthreadCondTimedwait. Returns 0,
// or -3 when the timed wait timed out. A null mutex, EPERM, EINVAL and any other error are
// reported to the engine's fatal error with the source line. The code after each report is kept
// as the original has it (a missing mutex still goes on to wait; after EPERM the unknown-error
// report follows; after EINVAL it returns -3). Ghidra treats the fatal error as non-returning and
// does not show those paths (QUIRKS.md).
//
// The community frame-rate patches replace the timed-wait call with a return, which would
// return to a garbage address if it ran: every wait the game makes here is untimed in practice.
// BB_TARGET_FPS does not change this function.
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

// The executable's call stubs for the system's condition waits.
RT_ORIGINAL(0x02fc0d78, sce_cond_wait, int32_t(void *cond, void *mutex));
RT_ORIGINAL(0x02fc0d68, sce_cond_timedwait, int32_t(void *cond, void *mutex, uint32_t timeout_us));

// The reports: PthreadCondition.cpp, and a message per error.
RT_GLOBAL(0x04d3c0af, source_file, const char);
RT_GLOBAL(0x04d3c158, message_invalid_parameter, const char);
RT_GLOBAL(0x04d3c177, message_einval, const char);
RT_GLOBAL(0x04d3c1c1, message_eperm, const char);
RT_GLOBAL(0x04d3c2ae, message_unknown_error, const char);
constexpr int32_t line_invalid_parameter = 0x9a, line_einval = 0xaf, line_eperm = 0xb3, line_unknown_error = 0xb5;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_eperm = static_cast<int32_t>(0x80020001), sce_einval = static_cast<int32_t>(0x80020016),
                  sce_etimedout = static_cast<int32_t>(0x8002003c);
constexpr int32_t wait_forever = -1;
constexpr uint32_t timed_out = static_cast<uint32_t>(-3);

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

}  // namespace

extern "C" uint32_t bb_kernel_condition_wait(void *cond, void *mutex, int32_t timeout_us)
{
    if (!mutex) report(line_invalid_parameter, message_invalid_parameter);
    const int32_t result = timeout_us == wait_forever
                               ? sce_cond_wait(cond, mutex)
                               : sce_cond_timedwait(cond, mutex, static_cast<uint32_t>(timeout_us));
    if (result >= 0) {
        if (result != 0) report(line_unknown_error, message_unknown_error);
        return 0;
    }
    if (result == sce_eperm) {
        report(line_eperm, message_eperm);
        report(line_unknown_error, message_unknown_error);
        return 0;
    }
    if (result == sce_einval) {
        report(line_einval, message_einval);
        return timed_out;
    }
    if (result == sce_etimedout) return timed_out;
    report(line_unknown_error, message_unknown_error);
    return 0;
}
