// SPDX-License-Identifier: GPL-2.0-or-later
// The fixed-size allocator's checked set-up (Dantelion2 DLFixedAllocator), shared by its own
// set-up function (fixed_allocator_initialize.cpp) and the owners that build it inline (the small
// object allocators).
#pragma once

#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"

namespace engine::fixed_allocator {

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLFixedAllocator.cpp"
RT_GLOBAL(0x04d3c555, initialize_source_file, const char);
// "align must be 2^n.(align = %d)"
RT_GLOBAL(0x04d3c5bc, message_alignment, const char);
constexpr int32_t line_alignment = 0x50;
// "chunk size is too small. ( chunk %d block %d)"
RT_GLOBAL(0x04d3c5db, message_chunk_too_small, const char);
constexpr int32_t line_chunk_too_small = 0x61;

// Sets an allocator up for elements of `element_size` in chunks whose elements may take `room`
// bytes. An alignment that is not a power of two is reported. An element takes at least 8 bytes
// (its first word links it into a free list) and is rounded up to the alignment. Room for fewer
// than two elements is reported. The chunk holds as many elements as fit in the room, followed by
// its descriptor. The reports return, and the set-up goes on after them, as the game does (an
// alignment of 0 passes the check and leaves an element size of 0, which the division by it does
// not survive).
inline void initialize_checked(Allocator *allocator, BackingAllocator *backing, uint64_t element_size, uint64_t room,
                               uint64_t alignment)
{
    constexpr uint64_t smallest_element = 8;
    constexpr uint64_t fewest_elements = 2;
    if ((alignment & (alignment - 1)) != 0)
        engine::fatal_error(initialize_source_file.address(), line_alignment, message_alignment.address(), alignment);
    const uint64_t element = element_size < smallest_element ? smallest_element : element_size;
    const uint64_t stride = (element + alignment - 1) & (0 - alignment);
    if (stride * fewest_elements > room)
        engine::fatal_error(initialize_source_file.address(), line_chunk_too_small, message_chunk_too_small.address(),
                            room, stride);
    set_up(allocator, backing, stride, room / stride, alignment);
}

}  // namespace engine::fixed_allocator
