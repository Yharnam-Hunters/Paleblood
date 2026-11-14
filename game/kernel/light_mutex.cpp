// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel (Dantelion2 core): the engine's light mutex (DLLightMutex).
//
// The object holds the system mutex at +0x08 and an "initialized" flag at +0x10; its third virtual
// function (0x0247ee80, 4 bytes: too short to hook) reads the flag, and finalize, initialize,
// lock, try lock and unlock ask through it. The destructors read the flag directly.
//
// - constructor (0x0247ecb0): the vtable and a cleared flag.
// - destructor (0x0247ed40): destroys an initialized mutex and clears the flag, unless it is
//   busy (EBUSY); the deleting destructor (0x0247ecc0) destroys it without clearing, then frees
//   the object. finalize (0x0247edc0): like the destructor, asking through the vtable; a busy
//   mutex returns -2. A destroy error is reported (EINVAL, then the unknown error).
// - initialize (0x0247ee40): nothing when initialized; otherwise the engine's PthreadMutex
//   initialize (0x02483fe0), whose nonzero result is returned with the flag left clear.
// - lock (0x0247ee90), try lock (0x0247ef80) and unlock (0x0247f070): the engine's PthreadMutex
//   lock (forever), try lock and unlock, inlined, with the same returns and report chains; a mutex
//   that is not initialized is reported first and then used anyway.
//
// The fatal error does not return in the game; the code after each report is kept as the
// original has it, as in kernel_condition_wait.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;

struct LightMutex;
struct LightMutexVtable {
    void *unknown_slots[2];
    bool (*is_initialized)(LightMutex *mutex);
};
struct LightMutex {
    const LightMutexVtable *vtable;
    MutexHandle handle;
    bool initialized;
};
static_assert(offsetof(LightMutexVtable, is_initialized) == 0x10);
static_assert(offsetof(LightMutex, handle) == 0x08 && offsetof(LightMutex, initialized) == 0x10);

RT_GLOBAL(0x057b2fa0, light_mutex_vtable, const LightMutexVtable);

// The engine's PthreadMutex initialize, the executable's call stubs for the system's mutexes,
// and the C++ delete.
RT_ORIGINAL(0x02483fe0, pthread_mutex_initialize, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbfee8, sce_mutex_destroy, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbfef8, sce_mutex_lock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbff08, sce_mutex_unlock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fc0cb8, sce_mutex_trylock, int32_t(MutexHandle *mutex));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

// The reports: the light mutex's own source file for a mutex that is not initialized, and
// PthreadMutex.cpp (where the inlined code comes from) for the system's errors.
RT_GLOBAL(0x04d3bcaa, light_source_file, const char);
RT_GLOBAL(0x04d3bd11, message_not_initialized, const char);
RT_GLOBAL(0x04d3c20e, mutex_source_file, const char);
RT_GLOBAL(0x04d3c275, message_invalid_attribute, const char);
RT_GLOBAL(0x04d3c2ae, message_unknown_error, const char);
RT_GLOBAL(0x04d3c2bd, message_deadlock, const char);
RT_GLOBAL(0x04d3c2f3, message_invalid_mutex, const char);
RT_GLOBAL(0x04d3c319, message_lock_limit, const char);
RT_GLOBAL(0x04d3c34d, message_runtime_error, const char);

constexpr int32_t line_lock_not_initialized = 0x7e, line_try_lock_not_initialized = 0x8c,
                  line_unlock_not_initialized = 0x99;
constexpr int32_t line_destroy_invalid = 0x55, line_destroy_unknown = 0x57;
constexpr int32_t line_lock_deadlock = 0x80, line_lock_invalid = 0x82, line_lock_limit = 0x84,
                  line_lock_runtime = 0x86;
constexpr int32_t line_try_lock_deadlock = 0xa5, line_try_lock_invalid = 0xa7, line_try_lock_limit = 0xa9,
                  line_try_lock_runtime = 0xab;
constexpr int32_t line_unlock_invalid = 0xc0, line_unlock_runtime = 0xc5;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_error(uint32_t code) { return static_cast<int32_t>(code); }
constexpr int32_t sce_eperm = sce_error(0x80020001), sce_edeadlk = sce_error(0x8002000b),
                  sce_ebusy = sce_error(0x80020010), sce_einval = sce_error(0x80020016),
                  sce_eagain = sce_error(0x80020023), sce_etimedout = sce_error(0x8002003c);

constexpr int32_t busy = -2, timed_out = -3;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(mutex_source_file.address(), line, message.address());
}

bool is_initialized(LightMutex *mutex) { return mutex->vtable->is_initialized(mutex); }

void require_initialized(LightMutex *mutex, int32_t line)
{
    if (!is_initialized(mutex))
        engine::fatal_error(light_source_file.address(), line, message_not_initialized.address());
}

// Destroys the system mutex; false when it is busy. Errors are reported.
bool destroy(LightMutex *mutex)
{
    const int32_t result = sce_mutex_destroy(&mutex->handle);
    if (result == sce_ebusy) return false;
    if (result != 0) {
        if (result == sce_einval) report(line_destroy_invalid, message_invalid_attribute);
        report(line_destroy_unknown, message_unknown_error);
    }
    return true;
}

}  // namespace

extern "C" void bb_kernel_light_mutex_construct(LightMutex *mutex)
{
    mutex->vtable = light_mutex_vtable.address();
    mutex->initialized = false;
}

extern "C" void bb_kernel_light_mutex_delete(LightMutex *mutex)
{
    mutex->vtable = light_mutex_vtable.address();
    if (mutex->initialized) destroy(mutex);
    operator_delete(mutex);
}

extern "C" void bb_kernel_light_mutex_destruct(LightMutex *mutex)
{
    mutex->vtable = light_mutex_vtable.address();
    if (mutex->initialized && destroy(mutex)) mutex->initialized = false;
}

extern "C" int32_t bb_kernel_light_mutex_finalize(LightMutex *mutex)
{
    if (!is_initialized(mutex)) return 0;
    if (!destroy(mutex)) return busy;
    mutex->initialized = false;
    return 0;
}

extern "C" int32_t bb_kernel_light_mutex_initialize(LightMutex *mutex)
{
    if (is_initialized(mutex)) return 0;
    const int32_t result = pthread_mutex_initialize(&mutex->handle);
    if (result != 0) return result;
    mutex->initialized = true;
    return 0;
}

extern "C" int32_t bb_kernel_light_mutex_lock(LightMutex *mutex)
{
    require_initialized(mutex, line_lock_not_initialized);
    const int32_t result = sce_mutex_lock(&mutex->handle);
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

extern "C" int32_t bb_kernel_light_mutex_try_lock(LightMutex *mutex)
{
    require_initialized(mutex, line_try_lock_not_initialized);
    const int32_t result = sce_mutex_trylock(&mutex->handle);
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

extern "C" int32_t bb_kernel_light_mutex_unlock(LightMutex *mutex)
{
    require_initialized(mutex, line_unlock_not_initialized);
    const int32_t result = sce_mutex_unlock(&mutex->handle);
    if (result == 0 || result == sce_eperm) return 0;
    if (result == sce_einval)
        report(line_unlock_invalid, message_invalid_mutex);
    else
        report(line_unlock_runtime, message_runtime_error);
    return 0;
}
