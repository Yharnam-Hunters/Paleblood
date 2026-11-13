// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fast fixed-size allocator's set-up (Dantelion2 DLFastFixedAllocator; layout in
// fast_fixed_allocator.h).
//
// engine_fast_fixed_allocator_initialize_02a3baa0 (0x02a3baa0) takes an allocator, the backing
// allocator its chunks will come from, a block size, a chunk size and an alignment. The chunk size
// is rounded up to a power of two: chunks are aligned to their size. A chunk size of 0x40 or less
// (the chunk's own header takes 0x40) is reported, and 0 again with its own message; an alignment
// that is not a power of two is reported. The alignment is at least 8, and the block size is
// rounded up to it. A chunk must have room for its header (0x20 rounded up to the alignment) and
// one block: the chunk's alignment and its size are each checked for that and reported. The
// blocks that fit after the 0x40-byte header make up a chunk; both chunk lists start empty. The
// reports return, and the set-up goes on after them, as the game does.
#include <cstdint>

#include "engine.h"
#include "fast_fixed_allocator.h"

namespace {

using namespace engine::fast_fixed_allocator;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLFastFixedAllocator.cpp"
RT_GLOBAL(0x04c690c7, source_file, const char);
// "The chunk size (%d) is below even the size of the management structure. Make the chunk size
// one that holds tens or hundreds of blocks." (in Japanese)
RT_GLOBAL(0x04c69132, message_chunk_below_header, const char);
constexpr int32_t line_chunk_below_header = 0xed;
// "faild chunkSize" (sic)
RT_GLOBAL(0x04c691f7, message_no_chunk_size, const char);
constexpr int32_t line_no_chunk_size = 0xef;
// "The block alignment (%d) is not a power of two" (in Japanese)
RT_GLOBAL(0x04c69207, message_alignment, const char);
constexpr int32_t line_alignment = 0xf8;
// "The chunk alignment (%d) is too small for a block (%d)." (in Japanese)
RT_GLOBAL(0x04c69251, message_chunk_alignment_too_small, const char);
constexpr int32_t line_chunk_alignment_too_small = 0x107;
// "The chunk size (%d) is too small for a block (%d)." (in Japanese)
RT_GLOBAL(0x04c692ac, message_chunk_too_small, const char);
constexpr int32_t line_chunk_too_small = 0x108;

constexpr uint64_t chunk_header_size = 0x40;
constexpr uint64_t chunk_header_fields = 0x20;  // the part of the header a block must clear
constexpr uint64_t smallest_alignment = 8;

template <typename... Values> void report(int32_t line, const char *message, Values... values)
{
    engine::fatal_error(source_file.address(), line, message, values...);
}

// The highest set bit of a value, as the game computes it (from the leading-zero count, the
// shift taken modulo 64: 0 gives the top bit).
uint64_t highest_bit(uint64_t value)
{
    constexpr int word_bits = 64;
    const int leading_zeros = value ? __builtin_clzll(value) : word_bits;
    return uint64_t{1} << ((word_bits - 1 - leading_zeros) & (word_bits - 1));
}

uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & (0 - alignment);
}

}  // namespace

extern "C" void bb_engine_fast_fixed_allocator_initialize_02a3baa0(Allocator *allocator,
                                                                  engine::fixed_allocator::BackingAllocator *backing,
                                                                  uint64_t block_size, uint64_t chunk_size,
                                                                  uint64_t alignment)
{
    if (chunk_size <= chunk_header_size) {
        report(line_chunk_below_header, message_chunk_below_header.address(), chunk_size);
        if (chunk_size == 0)
            report(line_no_chunk_size, message_no_chunk_size.address());
    }
    // The next power of two at or above the chunk size.
    const uint64_t chunk = highest_bit(chunk_size - 1 + highest_bit(chunk_size));
    if ((alignment & (alignment - 1)) != 0)
        report(line_alignment, message_alignment.address(), alignment);
    const uint64_t block_alignment = alignment < smallest_alignment ? smallest_alignment : alignment;
    allocator->alignment = block_alignment;
    const uint64_t chunk_alignment = align_up(chunk, block_alignment);
    const uint64_t block = align_up(block_size, block_alignment);
    const uint64_t header_and_block = align_up(chunk_header_fields, block_alignment) + block;
    allocator->chunk_mask = 0 - chunk_alignment;
    allocator->block_size = block;
    if (chunk_alignment <= header_and_block)
        report(line_chunk_alignment_too_small, message_chunk_alignment_too_small.address(), chunk_alignment, block);
    if (chunk <= header_and_block)
        report(line_chunk_too_small, message_chunk_too_small.address(), chunk, block);
    const uint64_t blocks = (chunk - chunk_header_size) / block;
    allocator->blocks_per_chunk = blocks;
    allocator->blocks_bytes = block * blocks;
    for (ListLinks &list : allocator->chunk_lists) {
        list.prev = &list;
        list.next = &list;
    }
    allocator->backing = backing;
    allocator->unknown_0x08 = nullptr;
    allocator->spare_chunk = nullptr;
}
