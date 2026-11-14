// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the heap manager's list of heap areas (Dantelion2 DLHeapManager.cpp).
//
// The heap manager starts with its own locked regular heap (locked_regular_heap.cpp). After it
// come a pointer to an engine array of areas, each a heap and the address range it manages,
// sorted by start, and a read-write lock (DLReadWriteLock) over that array. The array's buffers
// come from a locked regular heap, which the array reaches through its allocator pointer; the
// original inlines that heap's allocate and free (its light mutex locked around the regular
// heap's own allocate 0x02485730 and free 0x024858a0).
//
// register area (0x0247c000) adds [start, start + size) for a heap; no heap is refused (false).
// Under the write lock (taken forever, 0x0247f3d0) it walks back from the array's end past the
// areas that start above the new one. An area it stops on that holds the start but ends before
// the new area does is reported as a contradiction, and the walk carries on. When the area
// it stops on belongs to the same heap, nothing is added and the result is false. Otherwise the
// new area goes in after it (an MSVC-style vector insert; a full array grows by half, through
// the engine's transaction rule when the heap is out of room) and the result is true. The lock's
// unlock is inlined: a lock not created and an invalid lock are reported.
#include <cstddef>
#include <cstdint>

#include "container.h"
#include "engine.h"
#include "regular_heap.h"
#include "runtime/original.h"
#include "transaction.h"

namespace {

using engine::regular_heap::Heap;

constexpr int32_t wait_forever = -1;

// The light mutex of a locked regular heap: lock (with a timeout) and unlock through its vtable.
struct LightMutex;
struct LightMutexVtable {
    void *unknown_slots[3];
    int32_t (*lock)(LightMutex *mutex, int32_t timeout);
    void *unknown_slot_0x20;
    int32_t (*unlock)(LightMutex *mutex);
};
struct LightMutex {
    const LightMutexVtable *vtable;
    uintptr_t handle;
    bool initialized;
};
static_assert(offsetof(LightMutexVtable, lock) == 0x18 && offsetof(LightMutexVtable, unlock) == 0x28);

struct AllocatorInterface {
    const void *vtable;
    void *owner;
};

struct LockedRegularHeap {
    Heap heap;
    LightMutex mutex;
    AllocatorInterface allocator;
};
static_assert(offsetof(LockedRegularHeap, mutex) == 0x440 && sizeof(LockedRegularHeap) == 0x468);

// An area: the heap that manages [start, end).
struct Area {
    void *heap;
    uintptr_t start;
    uintptr_t end;
};
static_assert(sizeof(Area) == 0x18);

struct AreaArray {
    uint64_t unknown_0x00;
    Area *begin;
    Area *end;
    Area *capacity_end;
    LockedRegularHeap *heap;  // where the array's buffers come from
};
static_assert(offsetof(AreaArray, heap) == 0x20);

using LockHandle = uintptr_t;
struct ReadWriteLock;
struct ReadWriteLockVtable {
    void *unknown_slots[2];
    bool (*is_initialized)(ReadWriteLock *lock);
};
struct ReadWriteLock {
    const ReadWriteLockVtable *vtable;
    LockHandle handle;
};
static_assert(offsetof(ReadWriteLockVtable, is_initialized) == 0x10);

struct HeapManager {
    LockedRegularHeap own_heap;
    AreaArray *areas;
    ReadWriteLock areas_lock;
};
static_assert(offsetof(HeapManager, areas) == 0x468 && offsetof(HeapManager, areas_lock) == 0x470);

// The read-write lock's write lock, the system's unlock, and the regular heap's allocate and free.
RT_ORIGINAL(0x0247f3d0, read_write_lock_write, int32_t(ReadWriteLock *lock, int32_t timeout));
RT_ORIGINAL(0x02fc0ca8, sce_rwlock_unlock, int32_t(LockHandle *lock));
RT_ORIGINAL(0x02485730, regular_heap_allocate, Area *(Heap *heap, uint64_t size));
RT_ORIGINAL(0x024858a0, regular_heap_free, void(Heap *heap, void *pointer));

// The reports: the heap manager's own (a contradicting area), and the read-write lock's.
RT_GLOBAL(0x04d3ba46, heap_manager_source_file, const char);
RT_GLOBAL(0x04d3baaa, message_area_contradiction, const char);
RT_GLOBAL(0x04d3bd2b, lock_source_file, const char);
RT_GLOBAL(0x04d3be94, message_not_created, const char);
RT_GLOBAL(0x04d3be69, message_invalid_lock, const char);
constexpr int32_t line_area_contradiction = 0x19c, line_not_created = 0x130, line_invalid_lock = 0x13b;

constexpr int32_t sce_einval = static_cast<int32_t>(0x80020016);

Area *allocate_areas(LockedRegularHeap *heap, uint64_t count)
{
    const uint64_t size = count * sizeof(Area);
    heap->mutex.vtable->lock(&heap->mutex, wait_forever);
    Area *buffer = heap->heap.available >= size ? regular_heap_allocate(&heap->heap, size) : nullptr;
    heap->mutex.vtable->unlock(&heap->mutex);
    return buffer;
}

void free_areas(LockedRegularHeap *heap, Area *buffer)
{
    heap->mutex.vtable->lock(&heap->mutex, wait_forever);
    regular_heap_free(&heap->heap, buffer);
    heap->mutex.vtable->unlock(&heap->mutex);
}

// Placement construction, which skips a null address (only a failed allocation gives one).
void construct(Area *at, const Area &value)
{
    if (at != nullptr)
        *at = value;
}

void unlock(ReadWriteLock *lock)
{
    if (!lock->vtable->is_initialized(lock))
        engine::fatal_error(lock_source_file.address(), line_not_created, message_not_created.address());
    if (sce_rwlock_unlock(&lock->handle) == sce_einval)
        engine::fatal_error(lock_source_file.address(), line_invalid_lock, message_invalid_lock.address());
}

// Moves everything into a buffer one and a half times as big, with the area at `position`.
void grow_and_insert(AreaArray *areas, Area *position, const Area &area)
{
    const uint64_t count = areas->end - areas->begin;
    const uint64_t capacity = engine::array_grown_capacity<Area>(count);
    Area *buffer = allocate_areas(areas->heap, capacity);
    if (buffer == nullptr)
        engine::report_failed_allocation();

    const uint64_t index = position - areas->begin;
    construct(buffer + index, area);
    Area *to = buffer;
    for (Area *from = areas->begin; from != position; ++from)
        construct(to++, *from);
    to = buffer + index + 1;
    for (Area *from = position; from != areas->end; ++from)
        construct(to++, *from);

    if (areas->begin != nullptr)
        free_areas(areas->heap, areas->begin);
    areas->capacity_end = buffer + capacity;
    areas->end = buffer + count + 1;
    areas->begin = buffer;
}

void insert(AreaArray *areas, Area *position, const Area &area)
{
    Area *old_end = areas->end;
    if (old_end == areas->capacity_end) {
        grow_and_insert(areas, position, area);
        return;
    }
    if (position == old_end) {
        construct(old_end, area);
        areas->end = old_end + 1;
        return;
    }
    // Shift [position, end) up by one: the last area into the new slot, then the rest back to front.
    construct(old_end, old_end[-1]);
    areas->end = old_end + 1;
    for (Area *at = old_end - 1; at != position; --at)
        *at = at[-1];
    *position = area;
}

}  // namespace

extern "C" bool bb_engine_heap_manager_register_area_0247c000(HeapManager *manager, void *heap, uintptr_t start,
                                                               uint64_t size)
{
    if (heap == nullptr)
        return false;
    const uintptr_t end = start + size;
    read_write_lock_write(&manager->areas_lock, wait_forever);

    AreaArray *areas = manager->areas;
    Area *position = areas->end;
    while (position != areas->begin) {
        Area *before = position - 1;
        if (before->end > start) {
            if (before->start > start) {
                position = before;
                continue;
            }
            if (before->end < end)
                engine::fatal_error(heap_manager_source_file.address(), line_area_contradiction,
                                    message_area_contradiction.address());
        }
        if (before->heap == heap) {
            unlock(&manager->areas_lock);
            return false;
        }
        break;
    }

    insert(manager->areas, position, Area{heap, start, end});
    unlock(&manager->areas_lock);
    return true;
}
