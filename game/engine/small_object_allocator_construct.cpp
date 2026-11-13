// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the small object allocators' constructors (Dantelion2 DLSmallObjectAllocator; layout in
// small_object_allocator.h).
//
// engine_small_object_allocator_construct_02fa1e20 (0x02fa1e20) builds a plain small object
// allocator and engine_small_object_allocator_construct_locked_02fa22a0 (0x02fa22a0) a locked one,
// from a backing allocator, a chunk size, the largest and smallest object sizes and a granularity
// (character and system code builds them for its many small allocations). Both set their vtables
// and split the sizes into classes, one fixed-size allocator per class (set_up_classes). The
// locked one first creates its mutex (recursive) unless the mutex says it is already set up, and
// marks it set up when the creation succeeds.
#include <cstdint>

#include "small_object_allocator.h"

namespace {

using namespace engine::small_object_allocator;

RT_GLOBAL(0x0575b670, plain_vtable, const uint8_t);
RT_GLOBAL(0x056b5dc0, placeholder_lock_vtable, const uint8_t);
RT_GLOBAL(0x057b3240, locked_vtable, const uint8_t);
RT_GLOBAL(0x057b2fa0, mutex_vtable, const uint8_t);
// The mutex's own queries: whether it is set up, and the creation of its handle (0 on success).
RT_ORIGINAL(0x0247ee80, mutex_is_initialized, bool(Mutex *));
RT_ORIGINAL(0x02483fe0, mutex_create, int32_t(void **handle));

}  // namespace

extern "C" void bb_engine_small_object_allocator_construct_02fa1e20(Plain *allocator, BackingAllocator *backing,
                                                                   uint64_t chunk_size, uint64_t largest,
                                                                   uint64_t smallest, uint64_t granularity)
{
    allocator->vtable = plain_vtable.address();
    allocator->lock_vtable = vtable_entries(placeholder_lock_vtable.address());
    set_up_classes(&allocator->classes, &allocator->sizes, backing, chunk_size, largest, smallest, granularity);
}

extern "C" void bb_engine_small_object_allocator_construct_locked_02fa22a0(Locked *allocator,
                                                                          BackingAllocator *backing,
                                                                          uint64_t chunk_size, uint64_t largest,
                                                                          uint64_t smallest, uint64_t granularity)
{
    allocator->vtable = locked_vtable.address();
    allocator->lock.vtable = mutex_vtable.address();
    allocator->lock.initialized = false;
    if (!mutex_is_initialized(&allocator->lock) && mutex_create(&allocator->lock.handle) == 0)
        allocator->lock.initialized = true;
    set_up_classes(&allocator->classes, &allocator->sizes, backing, chunk_size, largest, smallest, granularity);
}
