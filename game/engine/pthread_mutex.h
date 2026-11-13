// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's mutex object (Dantelion2 PthreadMutex): a vtable, the system mutex handle and
// whether the handle was created. The engine's locked allocators hold one, and their destructors
// tear it down inline (finalize below).
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace engine::pthread_mutex {

struct Mutex;

struct MutexVtable {
    void *unknown_slots[2];
    bool (*is_initialized)(Mutex *);    // reads `initialized` (0x0247ee80)
};
static_assert(offsetof(MutexVtable, is_initialized) == 0x10);

struct Mutex {
    const MutexVtable *vtable;
    void *handle;
    bool initialized;
};
static_assert(offsetof(Mutex, handle) == 0x8 && offsetof(Mutex, initialized) == 0x10 && sizeof(Mutex) == 0x18);

// The mutex's vtable (its address point).
RT_GLOBAL(0x057b2fa0, mutex_vtable, const MutexVtable);
// The executable's call stub for scePthreadMutexDestroy.
RT_ORIGINAL(0x02fbfee8, sce_mutex_destroy, int32_t(void **handle));

// The executable's call stubs for scePthreadMutexLock and scePthreadMutexUnlock.
RT_ORIGINAL(0x02fbfef8, sce_mutex_lock, int32_t(void **handle));
RT_ORIGINAL(0x02fbff08, sce_mutex_unlock, int32_t(void **handle));

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_eperm = static_cast<int32_t>(0x80020001), sce_edeadlk = static_cast<int32_t>(0x8002000b),
                  sce_ebusy = static_cast<int32_t>(0x80020010), sce_einval = static_cast<int32_t>(0x80020016),
                  sce_eagain = static_cast<int32_t>(0x80020023), sce_etimedout = static_cast<int32_t>(0x8002003c);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\PthreadMutex.cpp"
RT_GLOBAL(0x04d3c20e, source_file, const char);
// "Logical Error. Invalid attribute value is given.(EINVAL)"
RT_GLOBAL(0x04d3c275, message_einval, const char);
// "Unknown Error."
RT_GLOBAL(0x04d3c2ae, message_unknown_error, const char);
// "Deadlock. Mutex is locked from owner thread.(EDEADLK)"
RT_GLOBAL(0x04d3c2bd, message_deadlock, const char);
// "Logical Error. Invalid mutex.(EINVAL)"
RT_GLOBAL(0x04d3c2f3, message_invalid_mutex, const char);
// "Logical Error. Max number of lock exceeded.(EAGAIN)"
RT_GLOBAL(0x04d3c319, message_too_many_locks, const char);
// "Runtime Error."
RT_GLOBAL(0x04d3c34d, message_runtime_error, const char);
constexpr int32_t line_einval = 0x55, line_unknown_error = 0x57;

// The source lines of a lock call's reports, in their order in the source.
struct LockReportLines {
    int32_t deadlock, invalid_mutex, too_many_locks, runtime_error;
};
constexpr LockReportLines lock_lines = {0x80, 0x82, 0x84, 0x86};
constexpr int32_t line_unlock_invalid_mutex = 0xc0, line_unlock_runtime_error = 0xc5;

// Reports a lock error: from its own message through the ones after it in the source (the
// reports return), as the shipped game does.
inline void report_lock_error(int32_t result, const LockReportLines &lines)
{
    const char *file = source_file.address();
    switch (result) {
    case sce_edeadlk:
        fatal_error(file, lines.deadlock, message_deadlock.address());
        [[fallthrough]];
    case sce_einval:
        fatal_error(file, lines.invalid_mutex, message_invalid_mutex.address());
        [[fallthrough]];
    case sce_eagain:
        fatal_error(file, lines.too_many_locks, message_too_many_locks.address());
        [[fallthrough]];
    default:
        fatal_error(file, lines.runtime_error, message_runtime_error.address());
    }
}

// Locks a handle; any result but success and ETIMEDOUT is reported. Returns the result.
inline int32_t lock_handle(void **handle)
{
    const int32_t result = sce_mutex_lock(handle);
    if (result != 0 && result != sce_etimedout)
        report_lock_error(result, lock_lines);
    return result;
}

// Unlocks a handle; EPERM (not the owner) is ignored, EINVAL and anything else reported.
inline void unlock_handle(void **handle)
{
    const int32_t result = sce_mutex_unlock(handle);
    if (result == 0 || result == sce_eperm)
        return;
    if (result == sce_einval)
        fatal_error(source_file.address(), line_unlock_invalid_mutex, message_invalid_mutex.address());
    else
        fatal_error(source_file.address(), line_unlock_runtime_error, message_runtime_error.address());
}

// Destroys the handle. A busy mutex is left alone (returns false: still initialized). Any other
// failure is reported (EINVAL first by name, then as unknown, since the report returns) and the
// mutex counts as gone (returns true), as on success.
inline bool destroy_system_handle(void **handle)
{
    const int32_t result = sce_mutex_destroy(handle);
    if (result == sce_ebusy)
        return false;
    if (result != 0) {
        if (result == sce_einval)
            fatal_error(source_file.address(), line_einval, message_einval.address());
        fatal_error(source_file.address(), line_unknown_error, message_unknown_error.address());
    }
    return true;
}

inline bool destroy_handle(Mutex *mutex)
{
    return destroy_system_handle(&mutex->handle);
}

// The mutex's finalization (0x0247edc0): when the vtable says it is initialized, destroys the
// handle and marks it uninitialized. Returns -2 when the mutex is busy (left initialized), else 0.
inline int32_t finalize_handle(Mutex *mutex)
{
    constexpr int32_t busy = -2;
    if (!mutex->vtable->is_initialized(mutex))
        return 0;
    if (!destroy_handle(mutex))
        return busy;
    mutex->initialized = false;
    return 0;
}

// The base destructor (0x0247ed40): resets the vtable and destroys the handle if it is still
// marked initialized (a busy mutex). A deleting destructor's inlined copy skips the last clear
// (the memory is freed right after), so its caller passes `clear_last = false`.
inline void destroy_base(Mutex *mutex, bool clear_last = true)
{
    mutex->vtable = mutex_vtable.address();
    if (mutex->initialized && destroy_handle(mutex) && clear_last)
        mutex->initialized = false;
}

// The mutex's destructor, as the allocators inline it: finalization, then the base destructor.
inline void finalize(Mutex *mutex, bool clear_last = true)
{
    finalize_handle(mutex);
    destroy_base(mutex, clear_last);
}

}  // namespace engine::pthread_mutex
