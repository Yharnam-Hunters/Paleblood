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
// Both report an object that was never created and go on.
// - engine_condition_signal_create_0247e690 (0x0247e690) creates the mutex and the condition
//   unless the vtable says the object is already created (returns 0). The argument byte becomes
//   the signalled state (shifted in whole, as in set) and the waited bit is cleared. A failed
//   mutex creation returns its result; a condition the system has no memory or resources for
//   (ENOMEM, EAGAIN) returns -1. Other errors are reported from their own message on (EINVAL,
//   EBUSY, unknown) and the object counts as created, as on success (returns 0).
// - engine_condition_signal_construct_0247e400 (0x0247e400): the vtable, and no flags.
// - engine_condition_signal_finalize_0247e5c0 (0x0247e5c0), when the vtable says the object is
//   created, destroys the mutex and then the condition: -2 when either is busy (the object stays
//   created), else 0 and no longer created. The mutex's errors are reported as in pthread_mutex.h;
//   the condition's EINVAL and other errors are reported (EINVAL then unknown).
// - engine_condition_signal_destroy_base_0247e4f0 (0x0247e4f0) and engine_condition_signal_delete_0247e420
//   (0x0247e420) reset the vtable and do the same when the created flag is set; the base destructor
//   then clears the flag (unless busy), the deleting one frees the object.
// - engine_condition_signal_poll_0247ea40 (0x0247ea40): 0 when the signal is set, else -2.
// - engine_condition_signal_check_0247ea80 (0x0247ea80) only reports an object that was never
//   created; returns 0. The lock and unlock are the engine's
// mutex calls inlined (pthread_mutex.h: errors are reported, a timed-out lock goes on).
#include <cstddef>
#include <cstdint>

#include "pthread_mutex.h"

using namespace engine::pthread_mutex;

namespace {

struct ConditionSignal;
struct ConditionSignalVtable {
    void *unknown_slots[2];
    bool (*is_created)(ConditionSignal *);
};
static_assert(offsetof(ConditionSignalVtable, is_created) == 0x10);

struct ConditionSignal {
    const ConditionSignalVtable *vtable;
    void *mutex_handle;
    void *condition;
    uint32_t flags;
};
static_assert(offsetof(ConditionSignal, condition) == 0x10 && offsetof(ConditionSignal, flags) == 0x18);

constexpr uint32_t flag_created = 1u << 0, flag_signalled = 1u << 1, flag_waited = 1u << 2;
constexpr int signalled_shift = 1;

// The executable's call stub for pthread_cond_broadcast (a POSIX error number on failure).
RT_ORIGINAL(0x02fbe848, posix_cond_broadcast, int32_t(void **condition));
// The object's vtable (its address point), and operator delete.
RT_GLOBAL(0x057b2f60, condition_signal_vtable, const ConditionSignalVtable);
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));
// The executable's call stub for pthread_cond_destroy (a POSIX error number on failure).
RT_ORIGINAL(0x02fbe808, posix_cond_destroy, int32_t(void **condition));
// The executable's call stub for pthread_cond_init (a POSIX error number on failure).
RT_ORIGINAL(0x02fbe7f8, posix_cond_init, int32_t(void **condition, const void *attributes));
// The engine's mutex creation (PthreadMutex.cpp).
RT_ORIGINAL(0x02483fe0, mutex_create, int32_t(void **handle));
// The condition wait (game/kernel/condition_wait.cpp).
RT_ORIGINAL(0x02483e80, condition_wait, uint32_t(void **condition, void **mutex_handle, int32_t timeout_us));

constexpr int32_t posix_enomem = 12, posix_ebusy = 16, posix_einval = 22, posix_eagain = 35;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLConditionSignal.cpp"
RT_GLOBAL(0x04d3bc26, signal_source_file, const char);
// "Sync object not created"
RT_GLOBAL(0x04d3bc92, message_not_created, const char);
// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\PthreadCondition.cpp"
RT_GLOBAL(0x04d3c0af, condition_source_file, const char);
// "Logical Error. Mutex is attempted to initialize twice.(EBUSY)" (sic: the condition's report)
RT_GLOBAL(0x04d3c11a, message_initialized_twice, const char);
constexpr int32_t line_destroy_einval = 0x52, line_destroy_unknown = 0x54;
constexpr int32_t line_poll_not_created = 0xc2, line_check_not_created = 0xd3;
constexpr int32_t line_init_einval = 0x34, line_init_twice = 0x36, line_init_unknown = 0x38;
constexpr int32_t line_set_not_created = 0x87, line_wait_not_created = 0xa7, line_broadcast_einval = 0x6a;

void check_created(const ConditionSignal *signal, int32_t line)
{
    if (!(signal->flags & flag_created))
        engine::fatal_error(signal_source_file.address(), line, message_not_created.address());
}

// pthread_cond_init's error reports: from the error's own message through the ones after it.
void report_init_error(int32_t error)
{
    const char *file = condition_source_file.address();
    switch (error) {
    case posix_einval:
        engine::fatal_error(file, line_init_einval, message_einval.address());
        [[fallthrough]];
    case posix_ebusy:
        engine::fatal_error(file, line_init_twice, message_initialized_twice.address());
        [[fallthrough]];
    default:
        engine::fatal_error(file, line_init_unknown, message_unknown_error.address());
    }
}

// Destroys the mutex, then the condition. False when either is busy (the rest is left alone).
bool destroy_objects(ConditionSignal *signal)
{
    if (!destroy_system_handle(&signal->mutex_handle))
        return false;
    const int32_t error = posix_cond_destroy(&signal->condition);
    if (error == 0)
        return true;
    if (error == posix_ebusy)
        return false;
    if (error == posix_einval)
        engine::fatal_error(condition_source_file.address(), line_destroy_einval, message_einval.address());
    engine::fatal_error(condition_source_file.address(), line_destroy_unknown, message_unknown_error.address());
    return true;
}

// The base destructor's work: the vtable reset and, when the created flag is set, the objects
// destroyed. Returns whether they are gone.
bool destroy_base(ConditionSignal *signal)
{
    signal->vtable = condition_signal_vtable.address();
    return (signal->flags & flag_created) && destroy_objects(signal);
}

}  // namespace

extern "C" void bb_engine_condition_signal_construct_0247e400(ConditionSignal *signal)
{
    signal->vtable = condition_signal_vtable.address();
    signal->flags = 0;
}

extern "C" void bb_engine_condition_signal_delete_0247e420(ConditionSignal *signal)
{
    destroy_base(signal);
    operator_delete(signal);
}

extern "C" void bb_engine_condition_signal_destroy_base_0247e4f0(ConditionSignal *signal)
{
    if (destroy_base(signal))
        signal->flags &= ~flag_created;
}

extern "C" int32_t bb_engine_condition_signal_finalize_0247e5c0(ConditionSignal *signal)
{
    constexpr int32_t busy = -2;
    if (!signal->vtable->is_created(signal))
        return 0;
    if (!destroy_objects(signal))
        return busy;
    signal->flags &= ~flag_created;
    return 0;
}

extern "C" int32_t bb_engine_condition_signal_poll_0247ea40(ConditionSignal *signal)
{
    constexpr int32_t not_set = -2;
    check_created(signal, line_poll_not_created);
    return (signal->flags & flag_signalled) ? 0 : not_set;
}

extern "C" int32_t bb_engine_condition_signal_check_0247ea80(ConditionSignal *signal)
{
    check_created(signal, line_check_not_created);
    return 0;
}

extern "C" int32_t bb_engine_condition_signal_create_0247e690(ConditionSignal *signal, uint8_t signalled)
{
    constexpr int32_t out_of_resources = -1;
    if (signal->vtable->is_created(signal))
        return 0;
    signal->flags = (signal->flags & ~(flag_signalled | flag_waited)) |
                    (static_cast<uint32_t>(signalled) << signalled_shift);
    const int32_t mutex_result = mutex_create(&signal->mutex_handle);
    if (mutex_result != 0)
        return mutex_result;
    const int32_t error = posix_cond_init(&signal->condition, nullptr);
    if (error == posix_enomem || error == posix_eagain)
        return out_of_resources;
    if (error != 0)
        report_init_error(error);
    signal->flags |= flag_created;
    return 0;
}

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
