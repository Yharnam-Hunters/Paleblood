// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel (Dantelion2 core): the engine's mutex (PthreadMutex.cpp in the engine).
//
// kernel_pthread_mutex_initialize (0x02483fe0): makes the mutex recursive through a mutex
// attribute, then initializes it. Out of memory or of mutexes returns -1.
// kernel_pthread_mutex_destroy (0x024840b0): destroys it. A busy mutex returns -2.
// kernel_pthread_mutex_lock (0x02484110): locks it, forever when the timeout is -1, else with
// scePthreadMutexTimedlock. A timed-out lock returns -3.
// kernel_pthread_mutex_try_lock (0x024841d0): tries the lock. A busy mutex returns -2.
// kernel_pthread_mutex_unlock (0x02484290): unlocks it. Unlocking a mutex the thread does not
// own (EPERM) is quietly accepted.
//
// Everything else returns 0. Each error the engine knows is reported to its fatal error with
// the source line, and then the reports of the errors after it in the original's chain follow
// (a deadlock in the lock is reported four times: deadlock, invalid mutex, lock limit, runtime
// error). The fatal error does not return in the game; the code after each report is kept as the
// original has it, as in kernel_condition_wait.
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;
using MutexAttributes = uintptr_t;

// The executable's call stubs for the system's mutexes.
RT_ORIGINAL(0x02fbfea8, sce_mutexattr_init, int32_t(MutexAttributes *attributes));
RT_ORIGINAL(0x02fbfeb8, sce_mutexattr_settype, int32_t(MutexAttributes *attributes, int32_t type));
RT_ORIGINAL(0x02fbfec8, sce_mutex_init,
            int32_t(MutexHandle *mutex, MutexAttributes *attributes, const char *name));
RT_ORIGINAL(0x02fbfed8, sce_mutexattr_destroy, int32_t(MutexAttributes *attributes));
RT_ORIGINAL(0x02fbfee8, sce_mutex_destroy, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbfef8, sce_mutex_lock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbff08, sce_mutex_unlock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fc0cb8, sce_mutex_trylock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fc0d88, sce_mutex_timedlock, int32_t(MutexHandle *mutex, uint32_t timeout_us));

// The reports: PthreadMutex.cpp, and a message per error.
RT_GLOBAL(0x04d3c20e, source_file, const char);
RT_GLOBAL(0x04d3c275, message_invalid_attribute, const char);
RT_GLOBAL(0x04d3c2ae, message_unknown_error, const char);
RT_GLOBAL(0x04d3c2bd, message_deadlock, const char);
RT_GLOBAL(0x04d3c2f3, message_invalid_mutex, const char);
RT_GLOBAL(0x04d3c319, message_lock_limit, const char);
RT_GLOBAL(0x04d3c34d, message_runtime_error, const char);

constexpr int32_t line_initialize_invalid = 0x39, line_initialize_unknown = 0x3b;
constexpr int32_t line_destroy_invalid = 0x55, line_destroy_unknown = 0x57;
constexpr int32_t line_lock_deadlock = 0x80, line_lock_invalid = 0x82, line_lock_limit = 0x84,
                  line_lock_runtime = 0x86;
constexpr int32_t line_try_lock_deadlock = 0xa5, line_try_lock_invalid = 0xa7, line_try_lock_limit = 0xa9,
                  line_try_lock_runtime = 0xab;
constexpr int32_t line_unlock_invalid = 0xc0, line_unlock_runtime = 0xc5;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_error(uint32_t code) { return static_cast<int32_t>(code); }
constexpr int32_t sce_eperm = sce_error(0x80020001), sce_edeadlk = sce_error(0x8002000b),
                  sce_enomem = sce_error(0x8002000c), sce_ebusy = sce_error(0x80020010),
                  sce_einval = sce_error(0x80020016), sce_eagain = sce_error(0x80020023),
                  sce_etimedout = sce_error(0x8002003c);

constexpr int32_t recursive_type = 2;  // SCE_PTHREAD_MUTEX_RECURSIVE
constexpr int32_t wait_forever = -1;
constexpr int32_t out_of_resources = -1, busy = -2, timed_out = -3;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

}  // namespace

extern "C" int32_t bb_kernel_pthread_mutex_initialize(MutexHandle *mutex)
{
    MutexAttributes attributes;
    sce_mutexattr_init(&attributes);
    sce_mutexattr_settype(&attributes, recursive_type);
    const int32_t result = sce_mutex_init(mutex, &attributes, nullptr);
    sce_mutexattr_destroy(&attributes);
    if (result == 0) return 0;
    if (result == sce_enomem || result == sce_eagain) return out_of_resources;
    if (result == sce_einval) report(line_initialize_invalid, message_invalid_attribute);
    report(line_initialize_unknown, message_unknown_error);
    return 0;
}

extern "C" int32_t bb_kernel_pthread_mutex_destroy(MutexHandle *mutex)
{
    const int32_t result = sce_mutex_destroy(mutex);
    if (result == 0) return 0;
    if (result == sce_ebusy) return busy;
    if (result == sce_einval) report(line_destroy_invalid, message_invalid_attribute);
    report(line_destroy_unknown, message_unknown_error);
    return 0;
}

extern "C" int32_t bb_kernel_pthread_mutex_lock(MutexHandle *mutex, int32_t timeout_us)
{
    const int32_t result = timeout_us == wait_forever
                               ? sce_mutex_lock(mutex)
                               : sce_mutex_timedlock(mutex, static_cast<uint32_t>(timeout_us));
    if (result == 0) return 0;
    if (result == sce_etimedout) return timed_out;
    switch (result) {
    case sce_edeadlk:
        report(line_lock_deadlock, message_deadlock);
        [[fallthrough]];
    case sce_einval:
        report(line_lock_invalid, message_invalid_mutex);
        [[fallthrough]];
    case sce_eagain:
        report(line_lock_limit, message_lock_limit);
        [[fallthrough]];
    default:
        report(line_lock_runtime, message_runtime_error);
    }
    return 0;
}

extern "C" int32_t bb_kernel_pthread_mutex_try_lock(MutexHandle *mutex)
{
    const int32_t result = sce_mutex_trylock(mutex);
    if (result == 0) return 0;
    if (result == sce_ebusy) return busy;
    switch (result) {
    case sce_edeadlk:
        report(line_try_lock_deadlock, message_deadlock);
        [[fallthrough]];
    case sce_einval:
        report(line_try_lock_invalid, message_invalid_mutex);
        [[fallthrough]];
    case sce_eagain:
        report(line_try_lock_limit, message_lock_limit);
        [[fallthrough]];
    default:
        report(line_try_lock_runtime, message_runtime_error);
    }
    return 0;
}

extern "C" int32_t bb_kernel_pthread_mutex_unlock(MutexHandle *mutex)
{
    const int32_t result = sce_mutex_unlock(mutex);
    if (result == 0 || result == sce_eperm) return 0;
    if (result == sce_einval)
        report(line_unlock_invalid, message_invalid_mutex);
    else
        report(line_unlock_runtime, message_runtime_error);
    return 0;
}
