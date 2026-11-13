// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fast small object allocators' constructors (Dantelion2 DLFastSmallObjectAllocator;
// layout in fast_small_object_allocator.h).
//
// engine_fast_small_object_allocator_construct_02fa1640 (0x02fa1640) builds a plain fast small
// object allocator and engine_fast_small_object_allocator_construct_locked_02fa1950 (0x02fa1950) a
// locked one, from a backing allocator, a chunk size, the largest and smallest object sizes, the
// step between class sizes and the largest alignment. Both set their vtable, no class array yet
// and the backing allocator, then their lock: the plain one a placeholder, the locked one its mutex,
// created unless the mutex says it is already set up and marked set up when the creation succeeds.
// Then they split the sizes into classes (set_up_classes).
#include <cstddef>
#include <cstdint>

#include "fast_small_object_allocator.h"

namespace {

using namespace engine::fast_small_object_allocator;

RT_GLOBAL(0x056b5dc0, placeholder_lock_vtable, const uint8_t);
constexpr std::ptrdiff_t vtable_entries_offset = 0x10;
// The mutex's own queries: whether it is set up, and the creation of its handle (0 on success).
RT_ORIGINAL(0x0247ee80, mutex_is_initialized, bool(engine::pthread_mutex::Mutex *));
RT_ORIGINAL(0x02483fe0, mutex_create, int32_t(void **handle));

}  // namespace

extern "C" void bb_engine_fast_small_object_allocator_construct_02fa1640(Plain *allocator, BackingAllocator *backing,
                                                                        uint64_t chunk_size, uint64_t largest,
                                                                        uint64_t smallest, uint64_t step,
                                                                        uint64_t max_alignment)
{
    allocator->base.vtable = plain_vtable.address();
    allocator->base.classes = nullptr;
    allocator->base.backing = backing;
    allocator->lock_vtable = placeholder_lock_vtable.address() + vtable_entries_offset;
    set_up_classes(&allocator->base, backing, chunk_size, largest, smallest, step, max_alignment);
}

extern "C" void bb_engine_fast_small_object_allocator_construct_locked_02fa1950(Locked *allocator,
                                                                               BackingAllocator *backing,
                                                                               uint64_t chunk_size, uint64_t largest,
                                                                               uint64_t smallest, uint64_t step,
                                                                               uint64_t max_alignment)
{
    allocator->base.vtable = locked_vtable.address();
    allocator->base.classes = nullptr;
    allocator->base.backing = backing;
    allocator->lock.vtable = engine::pthread_mutex::mutex_vtable.address();
    allocator->lock.initialized = false;
    if (!mutex_is_initialized(&allocator->lock) && mutex_create(&allocator->lock.handle) == 0)
        allocator->lock.initialized = true;
    set_up_classes(&allocator->base, backing, chunk_size, largest, smallest, step, max_alignment);
}
