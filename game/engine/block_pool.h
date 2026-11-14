// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's fixed-size block pool: pages of equal blocks taken from an allocator, each page's
// header right after its blocks (block_pool.cpp). The shader upload manager takes its range
// records from one (shader_upload_manager.h).
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"

namespace engine {

// A free block: its first word chains to the page's next free block.
struct FreeBlock {
    FreeBlock *next;
};

// Links of a page on one of the pool's two rings; the pool's ring heads are bare links.
struct BlockPoolLinks {
    BlockPoolLinks *next;
    BlockPoolLinks *previous;
};

// A page's header, at its start plus the pool's page size (the start is the header's address
// masked with the pool's page mask).
struct BlockPoolPage {
    BlockPoolLinks links;   // on a ring (not set while the page is the current one)
    FreeBlock *free_blocks;
    uint64_t free_count;
};
static_assert(offsetof(BlockPoolPage, free_blocks) == 0x10 && sizeof(BlockPoolPage) == 0x20);

struct BlockPool {
    BlockPoolPage *current;        // the page blocks are taken from, null until one is needed
    uint64_t page_count;
    BlockPoolLinks partial_pages;  // pages with free blocks (taken from the front)
    BlockPoolLinks full_pages;     // pages without
    uint64_t block_size;
    uint64_t unknown_0x38;
    uint64_t page_size;            // the bytes of blocks before the header
    uint64_t page_mask;            // clears a block's address down to its page's start
    uint64_t blocks_per_page;
    Allocator *allocator;
};
static_assert(offsetof(BlockPool, partial_pages) == 0x10 && offsetof(BlockPool, full_pages) == 0x20);
static_assert(offsetof(BlockPool, block_size) == 0x30 && offsetof(BlockPool, page_size) == 0x40);
static_assert(offsetof(BlockPool, blocks_per_page) == 0x50 && offsetof(BlockPool, allocator) == 0x58);

}  // namespace engine
