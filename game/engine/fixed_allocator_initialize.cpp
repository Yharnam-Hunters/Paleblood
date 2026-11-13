// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's set-up (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h, the checked set-up in fixed_allocator_initialize.h).
//
// engine_fixed_allocator_initialize_02484b30 (0x02484b30) takes an allocator, the backing
// allocator its chunks come from, an element size, the bytes each chunk's elements may take and an
// alignment, and sets the allocator up with no chunks. The heaps that own a descriptor allocator
// set theirs up inline, without checks; the small object allocators inline this checked set-up;
// the other owners call this function.
#include <cstdint>

#include "fixed_allocator_initialize.h"

extern "C" void bb_engine_fixed_allocator_initialize_02484b30(engine::fixed_allocator::Allocator *allocator,
                                                             engine::fixed_allocator::BackingAllocator *backing,
                                                             uint64_t element_size, uint64_t room,
                                                             uint64_t alignment)
{
    engine::fixed_allocator::initialize_checked(allocator, backing, element_size, room, alignment);
}
