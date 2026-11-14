// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the heap manager's locked regular heap (Dantelion2, DLHeapManager.cpp's neighbour).
//
// The object is a regular heap (regular_heap.h, 0x440 bytes), then a light mutex (DLLightMutex:
// its vtable, the system mutex's handle and an "initialized" flag), then a small allocator
// interface: its own vtable and a pointer back to the object. The heap manager builds one over a
// static buffer of its own during start-up.
//
// - construct (0x0247c590): the heap state zeroed (the C library's memset), the light mutex
//   constructed (its vtable, the flag cleared, the handle left as it was) and the interface set
//   to point back at the object.
// - initialize (0x0247c5e0): no memory is refused (false). A light mutex that its vtable says is
//   not initialized is initialized here, through the engine's PthreadMutex initialize
//   (0x02483fe0); when that fails the result is false and the flag stays clear. Then the regular
//   heap's own set-up (0x02485500, inlined in the original) takes the memory and gives the result.
#include <cstddef>
#include <cstdint>

#include "regular_heap.h"
#include "runtime/original.h"

namespace {

using engine::regular_heap::Heap;
using MutexHandle = uintptr_t;

struct LightMutex;
struct LightMutexVtable {
    void *unknown_slots[2];
    bool (*is_initialized)(LightMutex *mutex);
};
struct LightMutex {
    const LightMutexVtable *vtable;
    MutexHandle handle;
    bool initialized;
};

struct AllocatorInterface {
    const void *vtable;
    void *owner;
};

struct LockedRegularHeap {
    Heap heap;
    LightMutex mutex;
    AllocatorInterface allocator;
};
static_assert(offsetof(LightMutexVtable, is_initialized) == 0x10);
static_assert(offsetof(LockedRegularHeap, mutex) == 0x440);
static_assert(offsetof(LockedRegularHeap, mutex) + offsetof(LightMutex, handle) == 0x448);
static_assert(offsetof(LockedRegularHeap, mutex) + offsetof(LightMutex, initialized) == 0x450);
static_assert(offsetof(LockedRegularHeap, allocator) == 0x458 && sizeof(LockedRegularHeap) == 0x468);

RT_GLOBAL(0x057b2fa0, light_mutex_vtable, const LightMutexVtable);
RT_GLOBAL(0x057b2e70, allocator_interface_vtable, const uint8_t);

RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t, uint64_t));
RT_ORIGINAL(0x02483fe0, pthread_mutex_initialize, int32_t(MutexHandle *mutex));

// Initializes the light mutex unless its vtable says it already is; false when the system mutex
// can't be made.
bool ensure_mutex(LightMutex *mutex)
{
    if (mutex->vtable->is_initialized(mutex))
        return true;
    if (pthread_mutex_initialize(&mutex->handle) != 0)
        return false;
    mutex->initialized = true;
    return true;
}

}  // namespace

extern "C" void bb_engine_locked_regular_heap_construct_0247c590(LockedRegularHeap *object)
{
    c_memset(&object->heap, 0, sizeof(Heap));
    object->mutex.vtable = light_mutex_vtable.address();
    object->mutex.initialized = false;
    object->allocator.vtable = allocator_interface_vtable.address();
    object->allocator.owner = object;
}

extern "C" bool bb_engine_locked_regular_heap_initialize_0247c5e0(LockedRegularHeap *object, void *memory,
                                                                   uint64_t size)
{
    if (memory == nullptr)
        return false;
    if (!ensure_mutex(&object->mutex))
        return false;
    return bb_engine_regular_heap_initialize_02485500(&object->heap, memory, size);
}
