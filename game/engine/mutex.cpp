// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine mutex the debug print's lock is built on (vtable 0x056b5b70; the print lock
// sets its own vtable over it). Its layout is the PthreadMutex object's from pthread_mutex.h: a
// vtable whose third slot says whether the mutex is initialized, the system mutex handle at +0x08
// and the initialized flag at +0x10.
//
// - engine_mutex_construct_02a3a840 (0x02a3a840): sets the vtable and clears the flag; the
//   handle is left as it was.
// - engine_mutex_initialize_02a3a920 (0x02a3a920): asks the vtable whether the mutex is
//   initialized and returns 0 if so. Otherwise it creates the system mutex with the engine's
//   PthreadMutex initialize (0x02483fe0) and sets the flag when that returns 0; a nonzero result
//   is returned with the flag left as it was.
#include <cstdint>

#include "pthread_mutex.h"
#include "runtime/original.h"

namespace {

using engine::pthread_mutex::Mutex;
using engine::pthread_mutex::MutexVtable;

// The mutex's vtable (its address point).
RT_GLOBAL(0x056b5b70, mutex_vtable, const MutexVtable);
// The engine's PthreadMutex initialize: a recursive system mutex in the handle.
RT_ORIGINAL(0x02483fe0, pthread_mutex_initialize, int32_t(void **handle));

}  // namespace

extern "C" void bb_engine_mutex_construct_02a3a840(Mutex *mutex)
{
    mutex->vtable = mutex_vtable.address();
    mutex->initialized = false;
}

extern "C" int32_t bb_engine_mutex_initialize_02a3a920(Mutex *mutex)
{
    if (mutex->vtable->is_initialized(mutex))
        return 0;
    const int32_t result = pthread_mutex_initialize(&mutex->handle);
    if (result != 0)
        return result;
    mutex->initialized = true;
    return 0;
}
