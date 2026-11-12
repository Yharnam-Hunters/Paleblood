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

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\PthreadMutex.cpp"
RT_GLOBAL(0x04d3c20e, source_file, const char);
// "Logical Error. Invalid attribute value is given.(EINVAL)"
RT_GLOBAL(0x04d3c275, message_einval, const char);
// "Unknown Error."
RT_GLOBAL(0x04d3c2ae, message_unknown_error, const char);
constexpr int32_t line_einval = 0x55, line_unknown_error = 0x57;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_ebusy = static_cast<int32_t>(0x80020010), sce_einval = static_cast<int32_t>(0x80020016);

// Destroys the handle. A busy mutex is left alone (returns false: still initialized). Any other
// failure is reported (EINVAL first by name, then as unknown, since the report returns) and the
// mutex counts as gone (returns true), as on success.
inline bool destroy_handle(Mutex *mutex)
{
    const int32_t result = sce_mutex_destroy(&mutex->handle);
    if (result == sce_ebusy)
        return false;
    if (result != 0) {
        if (result == sce_einval)
            fatal_error(source_file.address(), line_einval, message_einval.address());
        fatal_error(source_file.address(), line_unknown_error, message_unknown_error.address());
    }
    return true;
}

// The mutex's destructor, as the allocators inline it: the mutex's own finalization asks the
// vtable whether it is initialized and destroys the handle; then the base destructor resets the
// vtable and destroys the handle again if it is still marked initialized (a busy mutex). A
// deleting destructor's copy skips the last clear (the memory is freed right after), so the
// caller passes `clear_last = false` there.
inline void finalize(Mutex *mutex, bool clear_last = true)
{
    if (mutex->vtable->is_initialized(mutex) && destroy_handle(mutex))
        mutex->initialized = false;
    mutex->vtable = mutex_vtable.address();
    if (mutex->initialized && destroy_handle(mutex) && clear_last)
        mutex->initialized = false;
}

}  // namespace engine::pthread_mutex
