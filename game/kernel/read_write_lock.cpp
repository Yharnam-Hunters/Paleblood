// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel (Dantelion2 core): the engine's read-write lock (DLReadWriteLock).
//
// The object holds the system read-write lock at +0x08 and an "initialized" flag at +0x10; its
// third virtual function (0x0247f3b0, 4 bytes: too short to hook) reads the flag, and the members
// below ask through it, except the destructors, which read the flag directly.
//
// - constructor (0x0247f180): the vtable and a cleared flag.
// - destructor (0x0247f1f0): destroys an initialized lock and clears the flag, unless it is
//   busy (EBUSY); the deleting destructor (0x0247f190) destroys it without clearing, then frees
//   the object.
// - finalize (0x0247f250): like the destructor, but asks through the vtable and returns -2 when
//   the lock is busy.
// - initialize (0x0247f2b0): does nothing when initialized. Otherwise it creates the system lock;
//   out of memory or of locks returns -1 with the flag left clear. Every other result sets the
//   flag, after reporting EPERM, EBUSY, EINVAL and EFAULT in one falling chain.
// - write lock (0x0247f3d0) and read lock (0x0247f640): forever when the timeout is -1, else
//   timed; a time-out returns -3. Try write lock (0x0247f4a0): busy returns -2.
// - unlock (0x0247f540, 0x0247f5c0 and 0x0247f710, the same code three times): a lock the
//   thread does not hold (EPERM) returns -4.
//
// Every other result returns 0. The engine's errors are reported to its fatal error with the
// source line; the reports after each in the original's chain follow, and a lock that is not
// initialized is reported and then used anyway. The fatal error does not return in the game; the
// code after each report is kept as the original has it, as in kernel_condition_wait.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

using RwlockHandle = uintptr_t;

struct ReadWriteLock;
struct ReadWriteLockVtable {
    void *unknown_slots[2];
    bool (*is_initialized)(ReadWriteLock *lock);
};
struct ReadWriteLock {
    const ReadWriteLockVtable *vtable;
    RwlockHandle handle;
    bool initialized;
};
static_assert(offsetof(ReadWriteLockVtable, is_initialized) == 0x10);
static_assert(offsetof(ReadWriteLock, handle) == 0x08 && offsetof(ReadWriteLock, initialized) == 0x10);

RT_GLOBAL(0x057b2fe0, read_write_lock_vtable, const ReadWriteLockVtable);

// The executable's call stubs for the system's read-write locks, and the C++ delete.
RT_ORIGINAL(0x02fc0cd8, sce_rwlock_init, int32_t(RwlockHandle *lock, const void *attributes, const char *name));
RT_ORIGINAL(0x02fc0cc8, sce_rwlock_destroy, int32_t(RwlockHandle *lock));
RT_ORIGINAL(0x02fc0d28, sce_rwlock_rdlock, int32_t(RwlockHandle *lock));
RT_ORIGINAL(0x02fc0d18, sce_rwlock_timedrdlock, int32_t(RwlockHandle *lock, uint32_t timeout_us));
RT_ORIGINAL(0x02fc0cf8, sce_rwlock_wrlock, int32_t(RwlockHandle *lock));
RT_ORIGINAL(0x02fc0ce8, sce_rwlock_timedwrlock, int32_t(RwlockHandle *lock, uint32_t timeout_us));
RT_ORIGINAL(0x02fc0d08, sce_rwlock_trywrlock, int32_t(RwlockHandle *lock));
RT_ORIGINAL(0x02fc0ca8, sce_rwlock_unlock, int32_t(RwlockHandle *lock));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

// The reports: the lock's source file, and a message per error.
RT_GLOBAL(0x04d3bd2b, source_file, const char);
RT_GLOBAL(0x04d3bd95, message_no_privilege, const char);
RT_GLOBAL(0x04d3bde8, message_initialized_twice, const char);
RT_GLOBAL(0x04d3c275, message_invalid_attribute, const char);
RT_GLOBAL(0x04d3be27, message_bad_address, const char);
RT_GLOBAL(0x04d3be69, message_invalid_lock, const char);
RT_GLOBAL(0x04d3be94, message_not_created, const char);
RT_GLOBAL(0x04d3beae, message_deadlock, const char);
RT_GLOBAL(0x04d3bedc, message_unknown_error, const char);
RT_GLOBAL(0x04d3beeb, message_lock_limit, const char);
RT_GLOBAL(0x04d3bf1a, message_deadlock_writing, const char);

constexpr int32_t line_initialize_no_privilege = 0x4d, line_initialize_twice = 0x4f,
                  line_initialize_invalid = 0x51, line_initialize_bad_address = 0x53;
constexpr int32_t line_destroy_invalid = 0x71;
constexpr int32_t line_write_not_created = 0xe6, line_write_invalid = 0xf7, line_write_deadlock = 0xf9,
                  line_write_unknown = 0xfb;
constexpr int32_t line_try_write_not_created = 0x111, line_try_write_invalid = 0x11b,
                  line_try_write_deadlock = 0x11d;
constexpr int32_t line_unlock_not_created = 0x130, line_unlock_invalid = 0x13b;
constexpr int32_t line_read_not_created = 0x15a, line_read_invalid = 0x16b, line_read_limit = 0x16d,
                  line_read_deadlock = 0x16f;

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_error(uint32_t code) { return static_cast<int32_t>(code); }
constexpr int32_t sce_eperm = sce_error(0x80020001), sce_edeadlk = sce_error(0x8002000b),
                  sce_enomem = sce_error(0x8002000c), sce_efault = sce_error(0x8002000e),
                  sce_ebusy = sce_error(0x80020010), sce_einval = sce_error(0x80020016),
                  sce_eagain = sce_error(0x80020023), sce_etimedout = sce_error(0x8002003c);

constexpr int32_t wait_forever = -1;
constexpr int32_t out_of_resources = -1, busy = -2, timed_out = -3, not_held = -4;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

bool is_initialized(ReadWriteLock *lock) { return lock->vtable->is_initialized(lock); }

void require_initialized(ReadWriteLock *lock, int32_t line)
{
    if (!is_initialized(lock)) report(line, message_not_created);
}

int32_t unlock(ReadWriteLock *lock)
{
    require_initialized(lock, line_unlock_not_created);
    const int32_t result = sce_rwlock_unlock(&lock->handle);
    if (result == sce_eperm) return not_held;
    if (result == sce_einval) report(line_unlock_invalid, message_invalid_lock);
    return 0;
}

}  // namespace

extern "C" void bb_kernel_read_write_lock_construct(ReadWriteLock *lock)
{
    lock->vtable = read_write_lock_vtable.address();
    lock->initialized = false;
}

extern "C" void bb_kernel_read_write_lock_delete(ReadWriteLock *lock)
{
    lock->vtable = read_write_lock_vtable.address();
    if (lock->initialized && sce_rwlock_destroy(&lock->handle) == sce_einval)
        report(line_destroy_invalid, message_invalid_lock);
    operator_delete(lock);
}

extern "C" void bb_kernel_read_write_lock_destruct(ReadWriteLock *lock)
{
    lock->vtable = read_write_lock_vtable.address();
    if (!lock->initialized) return;
    const int32_t result = sce_rwlock_destroy(&lock->handle);
    if (result == sce_ebusy) return;
    if (result == sce_einval) report(line_destroy_invalid, message_invalid_lock);
    lock->initialized = false;
}

extern "C" int32_t bb_kernel_read_write_lock_finalize(ReadWriteLock *lock)
{
    if (!is_initialized(lock)) return 0;
    const int32_t result = sce_rwlock_destroy(&lock->handle);
    if (result == sce_ebusy) return busy;
    if (result == sce_einval) report(line_destroy_invalid, message_invalid_lock);
    lock->initialized = false;
    return 0;
}

extern "C" int32_t bb_kernel_read_write_lock_initialize(ReadWriteLock *lock)
{
    if (is_initialized(lock)) return 0;
    const int32_t result = sce_rwlock_init(&lock->handle, nullptr, nullptr);
    if (result == sce_enomem || result == sce_eagain) return out_of_resources;
    switch (result) {
    case sce_eperm:
        report(line_initialize_no_privilege, message_no_privilege);
        [[fallthrough]];
    case sce_ebusy:
        report(line_initialize_twice, message_initialized_twice);
        [[fallthrough]];
    case sce_einval:
        report(line_initialize_invalid, message_invalid_attribute);
        [[fallthrough]];
    case sce_efault:
        report(line_initialize_bad_address, message_bad_address);
        break;
    default:
        break;
    }
    lock->initialized = true;
    return 0;
}

extern "C" int32_t bb_kernel_read_write_lock_write(ReadWriteLock *lock, int32_t timeout_us)
{
    require_initialized(lock, line_write_not_created);
    const int32_t result = timeout_us == wait_forever
                               ? sce_rwlock_wrlock(&lock->handle)
                               : sce_rwlock_timedwrlock(&lock->handle, static_cast<uint32_t>(timeout_us));
    if (result == 0) return 0;
    if (result == sce_etimedout) return timed_out;
    switch (result) {
    case sce_einval:
        report(line_write_invalid, message_invalid_lock);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_write_deadlock, message_deadlock);
        [[fallthrough]];
    default:
        report(line_write_unknown, message_unknown_error);
    }
    return 0;
}

extern "C" int32_t bb_kernel_read_write_lock_try_write(ReadWriteLock *lock)
{
    require_initialized(lock, line_try_write_not_created);
    const int32_t result = sce_rwlock_trywrlock(&lock->handle);
    if (result == sce_ebusy) return busy;
    switch (result) {
    case sce_einval:
        report(line_try_write_invalid, message_invalid_lock);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_try_write_deadlock, message_deadlock);
        break;
    default:
        break;
    }
    return 0;
}

extern "C" int32_t bb_kernel_read_write_lock_read(ReadWriteLock *lock, int32_t timeout_us)
{
    require_initialized(lock, line_read_not_created);
    const int32_t result = timeout_us == wait_forever
                               ? sce_rwlock_rdlock(&lock->handle)
                               : sce_rwlock_timedrdlock(&lock->handle, static_cast<uint32_t>(timeout_us));
    if (result == sce_etimedout) return timed_out;
    switch (result) {
    case sce_einval:
        report(line_read_invalid, message_invalid_lock);
        [[fallthrough]];
    case sce_eagain:
        report(line_read_limit, message_lock_limit);
        [[fallthrough]];
    case sce_edeadlk:
        report(line_read_deadlock, message_deadlock_writing);
        break;
    default:
        break;
    }
    return 0;
}

extern "C" int32_t bb_kernel_read_write_lock_unlock_0247f540(ReadWriteLock *lock) { return unlock(lock); }
extern "C" int32_t bb_kernel_read_write_lock_unlock_0247f5c0(ReadWriteLock *lock) { return unlock(lock); }
extern "C" int32_t bb_kernel_read_write_lock_unlock_0247f710(ReadWriteLock *lock) { return unlock(lock); }
