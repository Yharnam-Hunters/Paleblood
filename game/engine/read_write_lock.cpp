// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's read-write lock (Dantelion2 DLReadWriteLock): a vtable (slot 2 answers
// whether the lock was created) and the system rwlock handle.
//
// - engine_read_write_lock_read_lock_0247f640 (0x0247f640) takes the lock for reading, forever
//   when the timeout is -1, else up to the timeout in microseconds. Returns -3 on ETIMEDOUT, else
//   0. Errors are reported from their own message on through the ones after it in the source
//   (invalid id, lock limit, deadlock); any other result is ignored.
// - engine_read_write_lock_unlock_0247f540 (0x0247f540), engine_read_write_lock_unlock_0247f5c0
//   (0x0247f5c0) and engine_read_write_lock_unlock_0247f710 (0x0247f710) release it (three copies
//   of the same code: the read, write and plain unlocks). Return -4 on EPERM (not held); EINVAL is
//   reported; else 0.
// - engine_read_write_lock_write_lock_0247f3d0 (0x0247f3d0) takes it for writing, forever or up
//   to a timeout. Returns -3 on ETIMEDOUT, else 0. EINVAL and EDEADLK report their chains, and
//   any other failure or a positive result is reported as unknown.
// - engine_read_write_lock_try_write_lock_0247f4a0 (0x0247f4a0): -2 when the lock is busy; EINVAL
//   and EDEADLK report their chains; else 0.
// The lock and unlock calls each report a lock that was never created and go on.
// - engine_read_write_lock_create_0247f2b0 (0x0247f2b0) creates the system rwlock unless the
//   vtable says it is created (0). Returns -1 when the system is out of memory or resources
//   (ENOMEM, EAGAIN); EPERM, EBUSY, EINVAL and EFAULT report their chains, and the lock counts as
//   created after any other result (0).
// - engine_read_write_lock_finalize_0247f250 (0x0247f250) destroys it when the vtable says it is
//   created: -2 when busy (left created), else 0 (EINVAL is reported); then it is not created.
// - engine_read_write_lock_destroy_base_0247f1f0 (0x0247f1f0) and engine_read_write_lock_delete_0247f190
//   (0x0247f190) reset the vtable and destroy a handle still marked created (busy kept; EINVAL
//   reported); the base destructor then clears the flag, the deleting one frees the object.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace {

struct ReadWriteLock;

struct ReadWriteLockVtable {
    void *unknown_slots[2];
    bool (*is_created)(ReadWriteLock *);
};
static_assert(offsetof(ReadWriteLockVtable, is_created) == 0x10);

struct ReadWriteLock {
    const ReadWriteLockVtable *vtable;
    void *handle;
    bool created;
};
static_assert(offsetof(ReadWriteLock, created) == 0x10);

// The lock's vtable (its address point).
RT_GLOBAL(0x057b2fe0, read_write_lock_vtable, const ReadWriteLockVtable);
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// The executable's call stubs for the system rwlock calls.
RT_ORIGINAL(0x02fc0d28, sce_rwlock_rdlock, int32_t(void **handle));
RT_ORIGINAL(0x02fc0d18, sce_rwlock_timedrdlock, int32_t(void **handle, uint32_t timeout_us));
RT_ORIGINAL(0x02fc0ca8, sce_rwlock_unlock, int32_t(void **handle));
RT_ORIGINAL(0x02fc0cc8, sce_rwlock_destroy, int32_t(void **handle));
RT_ORIGINAL(0x02fc0cd8, sce_rwlock_init, int32_t(void **handle, const void *attributes, const char *name));
RT_ORIGINAL(0x02fc0ce8, sce_rwlock_timedwrlock, int32_t(void **handle, uint32_t timeout_us));
RT_ORIGINAL(0x02fc0cf8, sce_rwlock_wrlock, int32_t(void **handle));
RT_ORIGINAL(0x02fc0d08, sce_rwlock_trywrlock, int32_t(void **handle));

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_enomem = static_cast<int32_t>(0x8002000c), sce_efault = static_cast<int32_t>(0x8002000e),
                  sce_ebusy = static_cast<int32_t>(0x80020010);
constexpr int32_t sce_eperm = static_cast<int32_t>(0x80020001), sce_edeadlk = static_cast<int32_t>(0x8002000b),
                  sce_einval = static_cast<int32_t>(0x80020016), sce_eagain = static_cast<int32_t>(0x80020023),
                  sce_etimedout = static_cast<int32_t>(0x8002003c);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLReadWriteLock.cpp"
RT_GLOBAL(0x04d3bd2b, source_file, const char);
// "Sync object isn't created"
RT_GLOBAL(0x04d3be94, message_not_created, const char);
// "Logical Error. Given rwlock id is invalid."
RT_GLOBAL(0x04d3be69, message_invalid_id, const char);
// "Runtime Error. RWLock max lock limit exceeded."
RT_GLOBAL(0x04d3beeb, message_lock_limit, const char);
// "Deadlock. RWLock is locked for writing from owner thread."
RT_GLOBAL(0x04d3bf1a, message_deadlock, const char);
// "Deadlock. RWLock is locked from owner thread."
RT_GLOBAL(0x04d3beae, message_write_deadlock, const char);
// "Unknown error."
RT_GLOBAL(0x04d3bedc, message_unknown_error, const char);
// "Runtime Error. Executing user do not have priviledges to perform operation.(EPERM)"
RT_GLOBAL(0x04d3bd95, message_eperm, const char);
// "Logical Error. RWLock is attempted to initialize twice.(EBUSY)"
RT_GLOBAL(0x04d3bde8, message_initialized_twice, const char);
// "Logical Error. Invalid attribute value is given.(EINVAL)" (PthreadMutex.cpp's string)
RT_GLOBAL(0x04d3c275, message_invalid_attribute, const char);
// "Logical Error. Given address to OS API call is incorrect.(EFAULT)"
RT_GLOBAL(0x04d3be27, message_bad_address, const char);

constexpr int32_t line_unlock_not_created = 0x130, line_unlock_invalid_id = 0x13b;
constexpr int32_t line_destroy_invalid_id = 0x71;
constexpr int32_t line_create_eperm = 0x4d, line_create_twice = 0x4f, line_create_einval = 0x51,
                  line_create_bad_address = 0x53;
constexpr int32_t line_write_lock_not_created = 0xe6, line_write_lock_invalid_id = 0xf7,
                  line_write_lock_deadlock = 0xf9, line_write_lock_unknown = 0xfb;
constexpr int32_t line_try_write_lock_not_created = 0x111, line_try_write_lock_invalid_id = 0x11b,
                  line_try_write_lock_deadlock = 0x11d;
constexpr int32_t line_read_lock_not_created = 0x15a, line_read_lock_invalid_id = 0x16b,
                  line_read_lock_limit = 0x16d, line_read_lock_deadlock = 0x16f;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

void check_created(ReadWriteLock *lock, int32_t line)
{
    if (!lock->vtable->is_created(lock))
        report(line, message_not_created);
}

// Destroys the handle: false when the lock is busy (left as it is); EINVAL is reported.
bool destroy_handle(ReadWriteLock *lock)
{
    const int32_t result = sce_rwlock_destroy(&lock->handle);
    if (result == sce_ebusy)
        return false;
    if (result == sce_einval)
        report(line_destroy_invalid_id, message_invalid_id);
    return true;
}

// The base destructor's work: the vtable reset, and a handle still marked created destroyed.
// Returns whether the handle is gone.
bool destroy_base(ReadWriteLock *lock)
{
    lock->vtable = read_write_lock_vtable.address();
    return lock->created && destroy_handle(lock);
}

int32_t unlock(ReadWriteLock *lock)
{
    constexpr int32_t not_held = -4;
    check_created(lock, line_unlock_not_created);
    const int32_t result = sce_rwlock_unlock(&lock->handle);
    if (result == sce_eperm)
        return not_held;
    if (result == sce_einval)
        report(line_unlock_invalid_id, message_invalid_id);
    return 0;
}

}  // namespace

extern "C" int32_t bb_engine_read_write_lock_read_lock_0247f640(ReadWriteLock *lock, int32_t timeout_us)
{
    constexpr int32_t wait_forever = -1;
    constexpr int32_t timed_out = -3;
    check_created(lock, line_read_lock_not_created);
    const int32_t result = timeout_us == wait_forever
                               ? sce_rwlock_rdlock(&lock->handle)
                               : sce_rwlock_timedrdlock(&lock->handle, static_cast<uint32_t>(timeout_us));
    switch (result) {
    case sce_etimedout:
        return timed_out;
    case sce_einval:
        report(line_read_lock_invalid_id, message_invalid_id);
        [[fallthrough]];
    case sce_eagain:
        report(line_read_lock_limit, message_lock_limit);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_read_lock_deadlock, message_deadlock);
        break;
    default:
        break;
    }
    return 0;
}

extern "C" int32_t bb_engine_read_write_lock_unlock_0247f540(ReadWriteLock *lock)
{
    return unlock(lock);
}

extern "C" int32_t bb_engine_read_write_lock_unlock_0247f5c0(ReadWriteLock *lock)
{
    return unlock(lock);
}

extern "C" int32_t bb_engine_read_write_lock_unlock_0247f710(ReadWriteLock *lock)
{
    return unlock(lock);
}

extern "C" int32_t bb_engine_read_write_lock_write_lock_0247f3d0(ReadWriteLock *lock, int32_t timeout_us)
{
    constexpr int32_t wait_forever = -1;
    constexpr int32_t timed_out = -3;
    check_created(lock, line_write_lock_not_created);
    const int32_t result = timeout_us == wait_forever
                               ? sce_rwlock_wrlock(&lock->handle)
                               : sce_rwlock_timedwrlock(&lock->handle, static_cast<uint32_t>(timeout_us));
    if (result == 0)
        return 0;
    switch (result) {
    case sce_etimedout:
        return timed_out;
    case sce_einval:
        report(line_write_lock_invalid_id, message_invalid_id);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_write_lock_deadlock, message_write_deadlock);
        [[fallthrough]];
    default:
        report(line_write_lock_unknown, message_unknown_error);
    }
    return 0;
}

extern "C" int32_t bb_engine_read_write_lock_try_write_lock_0247f4a0(ReadWriteLock *lock)
{
    constexpr int32_t busy = -2;
    check_created(lock, line_try_write_lock_not_created);
    switch (sce_rwlock_trywrlock(&lock->handle)) {
    case sce_ebusy:
        return busy;
    case sce_einval:
        report(line_try_write_lock_invalid_id, message_invalid_id);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_try_write_lock_deadlock, message_write_deadlock);
        break;
    default:
        break;
    }
    return 0;
}

extern "C" int32_t bb_engine_read_write_lock_create_0247f2b0(ReadWriteLock *lock)
{
    constexpr int32_t out_of_resources = -1;
    if (lock->vtable->is_created(lock))
        return 0;
    // Each report returns, so an error's chain runs on through the messages after its own.
    switch (sce_rwlock_init(&lock->handle, nullptr, nullptr)) {
    case sce_enomem:
    case sce_eagain:
        return out_of_resources;
    case sce_eperm:
        report(line_create_eperm, message_eperm);
        [[fallthrough]];
    case sce_ebusy:
        report(line_create_twice, message_initialized_twice);
        [[fallthrough]];
    case sce_einval:
        report(line_create_einval, message_invalid_attribute);
        [[fallthrough]];
    case sce_efault:
        report(line_create_bad_address, message_bad_address);
        break;
    default:
        break;
    }
    lock->created = true;
    return 0;
}

extern "C" int32_t bb_engine_read_write_lock_finalize_0247f250(ReadWriteLock *lock)
{
    constexpr int32_t busy = -2;
    if (!lock->vtable->is_created(lock))
        return 0;
    if (!destroy_handle(lock))
        return busy;
    lock->created = false;
    return 0;
}

extern "C" void bb_engine_read_write_lock_destroy_base_0247f1f0(ReadWriteLock *lock)
{
    if (destroy_base(lock))
        lock->created = false;
}

extern "C" void bb_engine_read_write_lock_delete_0247f190(ReadWriteLock *lock)
{
    destroy_base(lock);
    operator_delete(lock);
}
