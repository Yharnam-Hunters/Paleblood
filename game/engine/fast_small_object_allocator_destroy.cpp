// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fast small object allocators' destructors (Dantelion2 DLFastSmallObjectAllocator;
// layout in fast_small_object_allocator.h).
//
// Each variant has a destructor and a deleting destructor, which ends in operator delete:
// - engine_fast_small_object_allocator_destroy_02484760 (0x02484760) and its deleting form
//   engine_fast_small_object_allocator_delete_024846f0 (0x024846f0), for the plain variant;
// - engine_fast_small_object_allocator_destroy_locked_02484910 (0x02484910) and its deleting form
//   engine_fast_small_object_allocator_delete_locked_024847d0 (0x024847d0), for the locked one.
// All set the variant's vtable, release every size class's fast fixed-size allocator and give the
// class array back to the backing allocator. The locked ones then tear the mutex down (finalize in
// pthread_mutex.h: a destroy failure is reported, a busy mutex is left marked initialized). The
// deleting forms skip the stores the deletion makes pointless: the plain one leaves the array
// pointer, the locked one the mutex's last flag clear.
#include "fast_small_object_allocator.h"

using namespace engine::fast_small_object_allocator;

extern "C" void bb_engine_fast_small_object_allocator_destroy_02484760(Allocator *allocator)
{
    allocator->vtable = plain_vtable.address();
    destroy_classes(allocator);
}

extern "C" void bb_engine_fast_small_object_allocator_delete_024846f0(Allocator *allocator)
{
    allocator->vtable = plain_vtable.address();
    destroy_classes(allocator, /*clear_array=*/false);
    operator_delete(allocator);
}

extern "C" void bb_engine_fast_small_object_allocator_destroy_locked_02484910(Locked *allocator)
{
    allocator->base.vtable = locked_vtable.address();
    destroy_classes(&allocator->base);
    engine::pthread_mutex::finalize(&allocator->lock);
}

extern "C" void bb_engine_fast_small_object_allocator_delete_locked_024847d0(Locked *allocator)
{
    allocator->base.vtable = locked_vtable.address();
    destroy_classes(&allocator->base);
    engine::pthread_mutex::finalize(&allocator->lock, /*clear_last=*/false);
    operator_delete(allocator);
}
