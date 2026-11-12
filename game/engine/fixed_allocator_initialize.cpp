// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's set-up (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_initialize_02484b30 (0x02484b30) takes an allocator, the backing
// allocator its chunks come from, an element size, the bytes each chunk's elements may take and an
// alignment. The heaps that own a descriptor allocator set theirs up inline; this is the
// out-of-line set-up the other owners call. An alignment that is not a power of two is reported. An
// element takes at least 8 bytes (its first word links it into a free list) and is rounded up to
// the alignment. Room for fewer than two elements is reported. The chunk holds as many elements as
// fit in the room, followed by its descriptor. The allocator starts with no chunks. The reports
// return, and the set-up goes on after them, as the game does (an alignment of 0 passes the check
// and leaves an element size of 0, which the division by it does not survive).
#include <cstdint>

#include "fixed_allocator.h"
#include "engine.h"

namespace {

using namespace engine::fixed_allocator;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLFixedAllocator.cpp"
RT_GLOBAL(0x04d3c555, source_file, const char);
// "align must be 2^n.(align = %d)"
RT_GLOBAL(0x04d3c5bc, message_alignment, const char);
constexpr int32_t line_alignment = 0x50;
// "chunk size is too small. ( chunk %d block %d)"
RT_GLOBAL(0x04d3c5db, message_chunk_too_small, const char);
constexpr int32_t line_chunk_too_small = 0x61;
constexpr uint64_t smallest_element = 8;
constexpr uint64_t fewest_elements = 2;

}  // namespace

extern "C" void bb_engine_fixed_allocator_initialize_02484b30(Allocator *allocator, BackingAllocator *backing,
                                                             uint64_t element_size, uint64_t room,
                                                             uint64_t alignment)
{
    if ((alignment & (alignment - 1)) != 0)
        engine::fatal_error(source_file.address(), line_alignment, message_alignment.address(), alignment);
    const uint64_t element = element_size < smallest_element ? smallest_element : element_size;
    const uint64_t stride = (element + alignment - 1) & (0 - alignment);
    if (stride * fewest_elements > room)
        engine::fatal_error(source_file.address(), line_chunk_too_small, message_chunk_too_small.address(), room,
                            stride);
    set_up(allocator, backing, stride, room / stride, alignment);
}
