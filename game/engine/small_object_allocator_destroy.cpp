// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the small object allocators' destructors (Dantelion2 DLSmallObjectAllocator; layout in
// small_object_allocator.h).
//
// - engine_small_object_allocator_destroy_0248aa90 (0x0248aa90) destroys a plain allocator and
//   engine_small_object_allocator_delete_0248a9f0 (0x0248a9f0) also deletes it;
// - engine_small_object_allocator_delete_locked_0248ab30 (0x0248ab30) destroys and deletes a
//   locked one.
// Each sets the variant's vtable and empties every size class's fixed-size allocator (its chunks
// go back to the backing allocator and it forgets them, as fixed_allocator_clear.cpp does), then
// gives the class array back to the backing allocator. The array pointer is cleared, except by the
// plain deleting form. The locked form then tears its mutex down (finalize in pthread_mutex.h),
// without the last flag clear, and both deleting forms end in operator delete.
#include <cstdint>

#include "small_object_allocator.h"

namespace {

using namespace engine::small_object_allocator;
namespace fixed_allocator = engine::fixed_allocator;

RT_GLOBAL(0x0575b670, plain_vtable, const uint8_t);
RT_GLOBAL(0x057b3240, locked_vtable, const uint8_t);
// The executable's call stub for operator delete.
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// Empties every class and frees the class array; clears the pointer to it when asked.
template <typename SmallObjectAllocator> void destroy_classes(SmallObjectAllocator *allocator, bool clear_array)
{
    for (uint64_t index = 0; index < allocator->sizes.class_count; ++index) {
        fixed_allocator::Allocator *size_class = allocator->classes + index;
        if (size_class->chunk_tree)
            fixed_allocator::release_chunks(size_class, size_class->chunk_tree);
        fixed_allocator::forget_chunks(size_class);
    }
    BackingAllocator *backing = allocator->sizes.backing;
    backing->vtable->free(backing, allocator->classes);
    if (clear_array)
        allocator->classes = nullptr;
}

}  // namespace

extern "C" void bb_engine_small_object_allocator_destroy_0248aa90(Plain *allocator)
{
    allocator->vtable = plain_vtable.address();
    destroy_classes(allocator, /*clear_array=*/true);
}

extern "C" void bb_engine_small_object_allocator_delete_0248a9f0(Plain *allocator)
{
    allocator->vtable = plain_vtable.address();
    destroy_classes(allocator, /*clear_array=*/false);
    operator_delete(allocator);
}

extern "C" void bb_engine_small_object_allocator_delete_locked_0248ab30(Locked *allocator)
{
    allocator->vtable = locked_vtable.address();
    destroy_classes(allocator, /*clear_array=*/true);
    engine::pthread_mutex::finalize(&allocator->lock, /*clear_last=*/false);
    operator_delete(allocator);
}
