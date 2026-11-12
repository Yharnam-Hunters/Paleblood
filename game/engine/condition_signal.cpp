// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's condition signal (Dantelion2 DLConditionSignal): an event that threads wait
// for, made of a mutex handle, a condition variable and flags (created, signalled, a waiter has
// waited).
//
// - engine_condition_signal_set_0247e780 (0x0247e780) sets or clears the signal under the mutex.
//   Setting it (the argument byte is exactly 1) wakes every waiter when one has waited since the
//   last wake-up; pthread_cond_broadcast's EINVAL is reported. Returns 0.
// - engine_condition_signal_wait_0247e8f0 (0x0247e8f0) waits under the mutex for the signal unless
//   it is already set (kernel_condition_wait, 0x02483e80, with the timeout in microseconds, -1
//   for none). Returns the wait's result, or 0 when it did not wait.
// Both report an object that was never created and go on. The lock and unlock are the engine's
// mutex calls inlined (pthread_mutex.h: errors are reported, a timed-out lock goes on).
#include <cstddef>
#include <cstdint>

#include "pthread_mutex.h"

using namespace engine::pthread_mutex;

namespace {

struct ConditionSignal {
    const void *vtable;
    void *mutex_handle;
    void *condition;
    uint32_t flags;
};
static_assert(offsetof(ConditionSignal, condition) == 0x10 && offsetof(ConditionSignal, flags) == 0x18);

constexpr uint32_t flag_created = 1u << 0, flag_signalled = 1u << 1, flag_waited = 1u << 2;
constexpr int signalled_shift = 1;

// The executable's call stub for pthread_cond_broadcast (a POSIX error number on failure).
RT_ORIGINAL(0x02fbe848, posix_cond_broadcast, int32_t(void **condition));
// The condition wait (game/kernel/condition_wait.cpp).
RT_ORIGINAL(0x02483e80, condition_wait, uint32_t(void **condition, void **mutex_handle, int32_t timeout_us));

constexpr int32_t posix_einval = 22;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLConditionSignal.cpp"
RT_GLOBAL(0x04d3bc26, signal_source_file, const char);
// "Sync object not created"
RT_GLOBAL(0x04d3bc92, message_not_created, const char);
// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\PthreadCondition.cpp"
RT_GLOBAL(0x04d3c0af, condition_source_file, const char);
constexpr int32_t line_set_not_created = 0x87, line_wait_not_created = 0xa7, line_broadcast_einval = 0x6a;

void check_created(const ConditionSignal *signal, int32_t line)
{
    if (!(signal->flags & flag_created))
        engine::fatal_error(signal_source_file.address(), line, message_not_created.address());
}

}  // namespace

extern "C" int32_t bb_engine_condition_signal_set_0247e780(ConditionSignal *signal, uint8_t signalled)
{
    check_created(signal, line_set_not_created);
    lock_handle(&signal->mutex_handle);
    const uint32_t flags = signal->flags;
    // (The argument byte is shifted in whole, so a byte above 1 sets the bits above too.)
    signal->flags = (flags & ~flag_signalled) | (static_cast<uint32_t>(signalled) << signalled_shift);
    if ((flags & flag_waited) && signalled == 1) {
        if (posix_cond_broadcast(&signal->condition) == posix_einval)
            engine::fatal_error(condition_source_file.address(), line_broadcast_einval, message_einval.address());
        signal->flags &= ~flag_waited;
    }
    unlock_handle(&signal->mutex_handle);
    return 0;
}

extern "C" uint32_t bb_engine_condition_signal_wait_0247e8f0(ConditionSignal *signal, int32_t timeout_us)
{
    check_created(signal, line_wait_not_created);
    lock_handle(&signal->mutex_handle);
    uint32_t result = 0;
    const uint32_t flags = signal->flags;
    if (!(flags & flag_signalled)) {
        signal->flags = flags | flag_waited;
        result = condition_wait(&signal->condition, &signal->mutex_handle, timeout_us);
    }
    unlock_handle(&signal->mutex_handle);
    return result;
}
