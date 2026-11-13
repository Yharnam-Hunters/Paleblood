// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's fast fixed-size allocator (Dantelion2 DLFastFixedAllocator): equal blocks carved
// from chunks that are aligned to their own size, so a block finds its chunk by masking its
// address.
#pragma once

#include <cstddef>
#include <cstdint>

#include "fixed_allocator.h"

namespace engine::fast_fixed_allocator {

// A circular list's sentinel: two links.
struct ListLinks {
    ListLinks *next;
    ListLinks *prev;
};

constexpr int chunk_list_count = 2;

struct Allocator {
    ListLinks *spare_chunk;     // a chunk on neither list (its links), or null; cleared by the set-up
    void *unknown_0x08;
    ListLinks chunk_lists[chunk_list_count];   // two chunk lists, empty after the set-up
    uint64_t block_size;        // the element size rounded up to the alignment
    uint64_t alignment;         // at least 8
    uint64_t blocks_bytes;      // what a chunk's blocks take: block_size * blocks_per_chunk
    uintptr_t chunk_mask;       // a block's address & chunk_mask = its chunk
    uint64_t blocks_per_chunk;
    fixed_allocator::BackingAllocator *backing;
};
static_assert(offsetof(Allocator, chunk_lists) == 0x10 && offsetof(Allocator, block_size) == 0x30);
static_assert(offsetof(Allocator, alignment) == 0x38 && offsetof(Allocator, blocks_bytes) == 0x40);
static_assert(offsetof(Allocator, chunk_mask) == 0x48 && offsetof(Allocator, blocks_per_chunk) == 0x50);
static_assert(offsetof(Allocator, backing) == 0x58 && sizeof(Allocator) == 0x60);

// A chunk's memory: its blocks, then its list links (at blocks_bytes).
inline void *chunk_memory(const Allocator *allocator, ListLinks *links)
{
    return reinterpret_cast<uint8_t *>(links) - allocator->blocks_bytes;
}

}  // namespace engine::fast_fixed_allocator
