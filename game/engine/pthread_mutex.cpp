// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's mutex (Dantelion2 PthreadMutex and the DLLightMutex wrapper; layout in
// pthread_mutex.h).
//
// - engine_pthread_mutex_create_02483fe0 (0x02483fe0) creates a recursive system mutex into a
//   handle. Returns 0, or -1 when the system is out of memory or resources (ENOMEM, EAGAIN); an
//   invalid attribute (EINVAL) and anything else are reported and return 0.
// - engine_pthread_mutex_finalize_0247edc0 (0x0247edc0) and engine_pthread_mutex_destroy_base_0247ed40
//   (0x0247ed40) tear a mutex down (finalize_handle and destroy_base in pthread_mutex.h).
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

using SystemAttributes = void *;

// The executable's call stubs for the system mutex calls.
RT_ORIGINAL(0x02fbfea8, sce_mutexattr_init, int32_t(SystemAttributes *));
RT_ORIGINAL(0x02fbfeb8, sce_mutexattr_settype, int32_t(SystemAttributes *, int32_t type));
RT_ORIGINAL(0x02fbfec8, sce_mutex_init, int32_t(void **handle, SystemAttributes *, const char *name));
RT_ORIGINAL(0x02fbfed8, sce_mutexattr_destroy, int32_t(SystemAttributes *));
RT_ORIGINAL(0x02fc0cb8, sce_mutex_trylock, int32_t(void **handle));

constexpr int32_t mutex_type_recursive = 2;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_enomem = static_cast<int32_t>(0x8002000c);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLLightMutex.cpp"
RT_GLOBAL(0x04d3bcaa, light_mutex_source_file, const char);
// "Mutex is not initialized."
RT_GLOBAL(0x04d3bd11, message_not_initialized, const char);

constexpr int32_t line_create_einval = 0x39, line_create_unknown = 0x3b;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

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

extern "C" int32_t bb_engine_pthread_mutex_create_02483fe0(void **handle)
{
    constexpr int32_t out_of_resources = -1;
    SystemAttributes attributes;
    sce_mutexattr_init(&attributes);
    sce_mutexattr_settype(&attributes, mutex_type_recursive);
    const int32_t result = sce_mutex_init(handle, &attributes, nullptr);
    sce_mutexattr_destroy(&attributes);
    if (result == 0)
        return 0;
    if (result == sce_enomem || result == sce_eagain)
        return out_of_resources;
    if (result == sce_einval)
        report(line_create_einval, message_einval);
    report(line_create_unknown, message_unknown_error);
    return 0;
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
