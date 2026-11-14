// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: emptying a fixed-size allocator (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_clear_02484c20 (0x02484c20) gives all of an allocator's chunks back to
// its backing allocator and forgets them. The element counts are left as they were.
#include "fixed_allocator.h"

using namespace engine::fixed_allocator;

extern "C" void bb_engine_fixed_allocator_clear_02484c20(Allocator *allocator)
{
    if (allocator->chunk_tree)
        release_chunks(allocator, allocator->chunk_tree);
    forget_chunks(allocator);
}
