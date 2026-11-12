// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's mutex (Dantelion2 PthreadMutex and the DLLightMutex wrapper; layout in
// pthread_mutex.h).
//
// - engine_pthread_mutex_finalize_0247edc0 (0x0247edc0) and engine_pthread_mutex_destroy_base_0247ed40
//   (0x0247ed40) tear a mutex down (finalize_handle and destroy_base in pthread_mutex.h), and
//   engine_pthread_mutex_delete_0247ecc0 (0x0247ecc0) is the deleting destructor: the base
//   destructor's work without the last clear, then the memory freed.
// - engine_pthread_mutex_initialize_0247ee40 (0x0247ee40) creates the handle (PthreadMutex.cpp's
//   creation, 0x02483fe0) unless the vtable says the mutex is initialized, and marks it initialized
//   when that succeeds. Returns the creation's result, 0 when it did not create.
// - engine_light_mutex_lock_0247ee90 (0x0247ee90), engine_light_mutex_try_lock_0247ef80
//   (0x0247ef80) and engine_light_mutex_unlock_0247f070 (0x0247f070) lock and unlock it (the
//   engine's hottest kernel calls). Each reports a mutex its vtable says is not initialized and
//   goes on. Lock returns -3 on ETIMEDOUT and try-lock -2 when the mutex is busy; every other
//   error is reported and returns 0. The reports run from the error's own message on through the
//   messages after it in the source (deadlock, invalid mutex, lock count exceeded, runtime error),
//   as the shipped game does. Unlock ignores EPERM (not the owner) and reports the rest.
#include <cstdint>

#include "pthread_mutex.h"

using namespace engine::pthread_mutex;

namespace {

RT_ORIGINAL(0x02483fe0, mutex_create, int32_t(void **handle));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));
// The executable's call stub for scePthreadMutexTrylock.
RT_ORIGINAL(0x02fc0cb8, sce_mutex_trylock, int32_t(void **handle));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLLightMutex.cpp"
RT_GLOBAL(0x04d3bcaa, light_mutex_source_file, const char);
// "Mutex is not initialized."
RT_GLOBAL(0x04d3bd11, message_not_initialized, const char);

void check_initialized(Mutex *mutex, int32_t line)
{
    if (!mutex->vtable->is_initialized(mutex))
        engine::fatal_error(light_mutex_source_file.address(), line, message_not_initialized.address());
}

// The source lines of each call's reports.
constexpr int32_t line_lock_not_initialized = 0x7e, line_try_lock_not_initialized = 0x8c,
                  line_unlock_not_initialized = 0x99;
constexpr LockReportLines try_lock_lines = {0xa5, 0xa7, 0xa9, 0xab};

}  // namespace

extern "C" void bb_engine_pthread_mutex_delete_0247ecc0(Mutex *mutex)
{
    destroy_base(mutex, false);
    operator_delete(mutex);
}

extern "C" int32_t bb_engine_pthread_mutex_initialize_0247ee40(Mutex *mutex)
{
    if (mutex->vtable->is_initialized(mutex))
        return 0;
    const int32_t result = mutex_create(&mutex->handle);
    if (result == 0)
        mutex->initialized = true;
    return result;
}

extern "C" int32_t bb_engine_pthread_mutex_finalize_0247edc0(Mutex *mutex)
{
    return finalize_handle(mutex);
}

extern "C" void bb_engine_pthread_mutex_destroy_base_0247ed40(Mutex *mutex)
{
    destroy_base(mutex);
}

extern "C" int32_t bb_engine_light_mutex_lock_0247ee90(Mutex *mutex)
{
    constexpr int32_t timed_out = -3;
    check_initialized(mutex, line_lock_not_initialized);
    return lock_handle(&mutex->handle) == sce_etimedout ? timed_out : 0;
}

extern "C" int32_t bb_engine_light_mutex_try_lock_0247ef80(Mutex *mutex)
{
    constexpr int32_t busy = -2;
    check_initialized(mutex, line_try_lock_not_initialized);
    const int32_t result = sce_mutex_trylock(&mutex->handle);
    if (result == 0)
        return 0;
    if (result == sce_ebusy)
        return busy;
    report_lock_error(result, try_lock_lines);
    return 0;
}

extern "C" int32_t bb_engine_light_mutex_unlock_0247f070(Mutex *mutex)
{
    check_initialized(mutex, line_unlock_not_initialized);
    unlock_handle(&mutex->handle);
    return 0;
}
