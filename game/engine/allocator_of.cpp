// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the allocator that owns a block of memory (Dantelion2's heap registry).
//
// engine_allocator_of_0247b720 (0x0247b720, allocator_of in frame_timing/task_manager.cpp) returns
// the allocator a pointer was allocated from, so that it can be freed there. The calling thread's
// temporary heap is asked first: when the pointer is inside its region, its allocator part is the
// answer. Otherwise the heap registry's list of heap ranges is searched, the last range first,
// under the registry's read-write lock; the first range that holds the pointer gives its allocator
// (even a null one). A pointer that no range holds gets the fallback allocator, asked for after the
// lock is released. Ranges hold [begin, end), compared unsigned.
//
// The release is the read-write lock's own (0x0247f710) inlined: a lock that isn't created is
// reported, and an invalid lock id returned by scePthreadRwlockUnlock is reported. Both reports
// carry on, as the game's fatal error does.
#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "engine.h"
#include "thread.h"
#include "runtime/original.h"

namespace {

// The thread's temporary heap (temporary_heap_create.cpp): its region, then its allocator part.
struct TemporaryHeap {
    uintptr_t begin;
    uintptr_t end;
    uint8_t unknown_0x10[0x10];
    engine::Allocator allocator;
};
static_assert(offsetof(TemporaryHeap, end) == 0x8 && offsetof(TemporaryHeap, allocator) == 0x20);
RT_GLOBAL(0x057e4568, thread_heap_offset, const int64_t);   // the heap pointer's place, from the thread pointer

// A registered heap: its allocator and the memory it owns.
struct HeapRange {
    engine::Allocator *allocator;
    uintptr_t begin;
    uintptr_t end;
};
static_assert(sizeof(HeapRange) == 0x18 && offsetof(HeapRange, begin) == 0x8 && offsetof(HeapRange, end) == 0x10);
struct HeapRangeList {
    void *unknown_0x00;
    HeapRange *begin;
    HeapRange *end;
};
static_assert(offsetof(HeapRangeList, begin) == 0x8 && offsetof(HeapRangeList, end) == 0x10);

// Dantelion2 DLReadWriteLock: a vtable whose slot +0x10 says the lock was created, then the
// system read-write lock.
struct ReadWriteLock;
struct ReadWriteLockVtable {
    void *unknown_slots[2];
    bool (*created)(ReadWriteLock *);
};
static_assert(offsetof(ReadWriteLockVtable, created) == 0x10);
struct ReadWriteLock {
    const ReadWriteLockVtable *vtable;
    void *system_lock;
};
static_assert(offsetof(ReadWriteLock, system_lock) == 0x8);
constexpr int32_t wait_forever = -1;

// The heap registry (the object 0x0247bbf0 returns): the heap ranges and their lock.
struct HeapRegistry {
    uint8_t unknown_0x00[0x468];
    HeapRangeList *ranges;
    ReadWriteLock lock;
};
static_assert(offsetof(HeapRegistry, ranges) == 0x468 && offsetof(HeapRegistry, lock) == 0x470);

RT_ORIGINAL(0x0247bbf0, heap_registry, HeapRegistry *());
RT_ORIGINAL(0x0247f640, read_write_lock_read, uint64_t(ReadWriteLock *, int32_t timeout));
RT_ORIGINAL(0x02fc0ca8, sce_rwlock_unlock, int32_t(void **system_lock));   // scePthreadRwlockUnlock
RT_ORIGINAL(0x0247fa50, fallback_allocator, engine::Allocator *());
constexpr int32_t sce_error_invalid = static_cast<int32_t>(0x80020016);   // SCE_KERNEL_ERROR_EINVAL

// The lock's reports: the lock's source file, and the line and message of each.
RT_GLOBAL(0x04d3bd2b, lock_source_file, const char);
RT_GLOBAL(0x04d3be94, message_not_created, const char);
RT_GLOBAL(0x04d3be69, message_invalid_lock, const char);
constexpr int32_t line_not_created = 0x130;
constexpr int32_t line_invalid_lock = 0x13b;

void read_write_lock_release(ReadWriteLock *lock)
{
    if (!lock->vtable->created(lock))
        engine::fatal_error(lock_source_file.address(), line_not_created, message_not_created.address());
    if (sce_rwlock_unlock(&lock->system_lock) == sce_error_invalid)
        engine::fatal_error(lock_source_file.address(), line_invalid_lock, message_invalid_lock.address());
}

}  // namespace

extern "C" engine::Allocator *bb_engine_allocator_of_0247b720(const void *data)
{
    HeapRegistry *registry = heap_registry();
    const uintptr_t address = reinterpret_cast<uintptr_t>(data);

    TemporaryHeap *heap = *reinterpret_cast<TemporaryHeap **>(engine::thread_pointer() + thread_heap_offset.get());
    if (heap && heap->begin <= address && address < heap->end) return &heap->allocator;

    read_write_lock_read(&registry->lock, wait_forever);
    HeapRange *first = registry->ranges->begin;
    for (HeapRange *range = registry->ranges->end; range != first;) {
        --range;
        if (range->begin <= address && address < range->end) {
            engine::Allocator *owner = range->allocator;
            read_write_lock_release(&registry->lock);
            return owner;
        }
    }
    read_write_lock_release(&registry->lock);
    return fallback_allocator();
}
