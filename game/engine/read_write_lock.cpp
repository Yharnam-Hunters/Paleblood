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
// Each reports a lock that was never created and goes on.
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
};

// The executable's call stubs for the system rwlock calls.
RT_ORIGINAL(0x02fc0d28, sce_rwlock_rdlock, int32_t(void **handle));
RT_ORIGINAL(0x02fc0d18, sce_rwlock_timedrdlock, int32_t(void **handle, uint32_t timeout_us));
RT_ORIGINAL(0x02fc0ca8, sce_rwlock_unlock, int32_t(void **handle));

// SCE error codes (0x80020000 plus the FreeBSD error number)
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

constexpr int32_t line_unlock_not_created = 0x130, line_unlock_invalid_id = 0x13b;
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
