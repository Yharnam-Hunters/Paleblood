// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the debug print the error reports write their messages through (engine.h's fatal_error
// prints its file, line and message with it).
//
// engine_debug_print_024df920 (0x024df920) takes a format and its arguments,
// engine_debug_print_list_024dfb30 (0x024dfb30) the same with a va_list. Both serialize printing
// behind one print lock, an engine mutex (vtable 0x057b3c20: lock with a timeout at slot 3, unlock
// at slot 5) with an "enabled" byte, created on first use in static storage:
// - First a light mutex beside it is constructed once (a C++ static guard, then its own
//   "constructed" byte); nothing here uses it afterwards.
// - Then, while the lock's instance pointer is still empty, a second static guard is taken and at
//   once released (its object has no initializer), and the lock is built in its storage: the mutex
//   constructor, the print lock's vtable, the mutex's initialize, the byte at +0x47 cleared and
//   printing enabled. The instance pointer then points at it.
// The print waits for the lock forever, prints through vprintf when `force` is set or printing is
// enabled, and unlocks. The variadic one builds its own va_list and does the same.
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

// The light mutex constructed beside the print lock: a vtable, the system mutex and a "created" flag.
struct LightMutex {
    const void *vtable;
    uint64_t handle;
    bool created;
};
static_assert(offsetof(LightMutex, created) == 0x10);

struct PrintLock;

// The engine mutex's virtual functions as the print uses them.
struct PrintLockVtable {
    void *destroy;
    void *destroy_and_free;
    bool (*is_created)(PrintLock *);
    int32_t (*lock)(PrintLock *, int32_t timeout);
    void *try_lock;
    void (*unlock)(PrintLock *);
};

// Where the print lock's own bytes start; what lies between the mutex and them is never read here.
constexpr size_t print_lock_fields_offset = 0x47;

struct PrintLock {
    const PrintLockVtable *vtable;
    uint64_t handle;
    bool created;
    uint8_t unread[print_lock_fields_offset - sizeof(LightMutex::created) - offsetof(LightMutex, created)];
    uint8_t cleared_at_creation;  // +0x47: cleared when the lock is built; the print never reads it
    uint8_t enabled;              // +0x48: any nonzero value prints
};
static_assert(offsetof(PrintLock, cleared_at_creation) == print_lock_fields_offset);
static_assert(offsetof(PrintLock, enabled) == 0x48);

constexpr int32_t wait_forever = -1;

// A C++ static guard: its first byte is set once the object is constructed.
using StaticGuard = uint64_t;

RT_ORIGINAL(0x02fbe538, static_guard_acquire, int32_t(StaticGuard *));
RT_ORIGINAL(0x02fbe558, static_guard_release, void(StaticGuard *));
RT_ORIGINAL(0x02fc08a8, c_vprintf, int32_t(const char *, va_list));
RT_ORIGINAL(0x0247ee80, light_mutex_is_created, bool(LightMutex *));
RT_ORIGINAL(0x02483fe0, system_mutex_create, int32_t(uint64_t *handle));
RT_ORIGINAL(0x02a3a840, mutex_construct, void(PrintLock *));
RT_ORIGINAL(0x02a3a920, mutex_initialize, int32_t(PrintLock *));

RT_GLOBAL(0x05aa5488, print_lock_instance, PrintLock *);
RT_GLOBAL(0x05aa5490, print_lock_storage, PrintLock);
RT_GLOBAL(0x05aa54e8, print_lock_guard, StaticGuard);
RT_GLOBAL(0x05aa5720, light_mutex_storage, LightMutex);
RT_GLOBAL(0x05aa573f, light_mutex_constructed, bool);
RT_GLOBAL(0x05aa5740, light_mutex_guard, StaticGuard);
RT_GLOBAL(0x056b6230, light_mutex_vtable, const void *);
RT_GLOBAL(0x057b3c20, print_lock_vtable, const PrintLockVtable);

// The first time through, takes the guard (the caller runs the initializer, then releases it).
bool guard_first_time(StaticGuard *guard)
{
    return *reinterpret_cast<const uint8_t *>(guard) == 0 && static_guard_acquire(guard) != 0;
}

void construct_light_mutex_once()
{
    StaticGuard *guard = light_mutex_guard.address();
    if (guard_first_time(guard)) {
        light_mutex_constructed.get() = false;
        static_guard_release(guard);
    }
    if (light_mutex_constructed.get())
        return;
    LightMutex *mutex = light_mutex_storage.address();
    mutex->created = false;
    mutex->vtable = light_mutex_vtable.address();
    if (!light_mutex_is_created(mutex) && system_mutex_create(&mutex->handle) == 0)
        mutex->created = true;
    light_mutex_constructed.get() = true;
}

PrintLock *print_lock()
{
    PrintLock *lock = print_lock_instance.get();
    if (lock != nullptr)
        return lock;
    construct_light_mutex_once();
    lock = print_lock_instance.get();
    if (lock != nullptr)
        return lock;
    StaticGuard *guard = print_lock_guard.address();
    if (guard_first_time(guard))
        static_guard_release(guard);
    lock = print_lock_storage.address();
    mutex_construct(lock);
    lock->vtable = print_lock_vtable.address();
    mutex_initialize(lock);
    lock->cleared_at_creation = 0;
    lock->enabled = 1;
    print_lock_instance.get() = lock;
    return lock;
}

}  // namespace

extern "C" void bb_engine_debug_print_list_024dfb30(bool force, const char *format, va_list arguments)
{
    PrintLock *lock = print_lock();
    lock->vtable->lock(lock, wait_forever);
    if (force || lock->enabled != 0)
        c_vprintf(format, arguments);
    lock->vtable->unlock(lock);
}

extern "C" void bb_engine_debug_print_024df920(bool force, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    bb_engine_debug_print_list_024dfb30(force, format, arguments);
    va_end(arguments);
}
