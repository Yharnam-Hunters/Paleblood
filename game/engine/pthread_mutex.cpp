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
RT_ORIGINAL(0x02fbfef8, sce_mutex_lock, int32_t(void **handle));
RT_ORIGINAL(0x02fc0cb8, sce_mutex_trylock, int32_t(void **handle));
RT_ORIGINAL(0x02fbff08, sce_mutex_unlock, int32_t(void **handle));

constexpr int32_t mutex_type_recursive = 2;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_eperm = static_cast<int32_t>(0x80020001), sce_edeadlk = static_cast<int32_t>(0x8002000b),
                  sce_enomem = static_cast<int32_t>(0x8002000c), sce_eagain = static_cast<int32_t>(0x80020023),
                  sce_etimedout = static_cast<int32_t>(0x8002003c);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLLightMutex.cpp"
RT_GLOBAL(0x04d3bcaa, light_mutex_source_file, const char);
// "Mutex is not initialized."
RT_GLOBAL(0x04d3bd11, message_not_initialized, const char);
// "Deadlock. Mutex is locked from owner thread.(EDEADLK)"
RT_GLOBAL(0x04d3c2bd, message_deadlock, const char);
// "Logical Error. Invalid mutex.(EINVAL)"
RT_GLOBAL(0x04d3c2f3, message_invalid_mutex, const char);
// "Logical Error. Max number of lock exceeded.(EAGAIN)"
RT_GLOBAL(0x04d3c319, message_too_many_locks, const char);
// "Runtime Error."
RT_GLOBAL(0x04d3c34d, message_runtime_error, const char);

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

// The source lines of a lock call's reports, in their order in the source.
struct LockReportLines {
    int32_t deadlock, invalid_mutex, too_many_locks, runtime_error;
};

// The source lines of each call's reports.
constexpr int32_t line_lock_not_initialized = 0x7e, line_try_lock_not_initialized = 0x8c,
                  line_unlock_not_initialized = 0x99;
constexpr LockReportLines lock_lines = {0x80, 0x82, 0x84, 0x86};
constexpr LockReportLines try_lock_lines = {0xa5, 0xa7, 0xa9, 0xab};
constexpr int32_t line_unlock_invalid_mutex = 0xc0, line_unlock_runtime_error = 0xc5;

// Reports a lock error: from its own message through the ones after it.
void report_lock_error(int32_t result, const LockReportLines &lines)
{
    switch (result) {
    case sce_edeadlk:
        report(lines.deadlock, message_deadlock);
        [[fallthrough]];
    case sce_einval:
        report(lines.invalid_mutex, message_invalid_mutex);
        [[fallthrough]];
    case sce_eagain:
        report(lines.too_many_locks, message_too_many_locks);
        [[fallthrough]];
    default:
        report(lines.runtime_error, message_runtime_error);
    }
}

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
    const int32_t result = sce_mutex_lock(&mutex->handle);
    if (result == 0)
        return 0;
    if (result == sce_etimedout)
        return timed_out;
    report_lock_error(result, lock_lines);
    return 0;
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
    const int32_t result = sce_mutex_unlock(&mutex->handle);
    if (result == 0 || result == sce_eperm)
        return 0;
    if (result == sce_einval)
        report(line_unlock_invalid_mutex, message_invalid_mutex);
    else
        report(line_unlock_runtime_error, message_runtime_error);
    return 0;
}
