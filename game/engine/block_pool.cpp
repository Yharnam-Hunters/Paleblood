// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size block pool's allocate and free (block_pool.h).
//
// - engine_block_pool_allocate_02a3b8b0: takes the current page's first free block. Without a
//   current page the front partial page becomes current; without one a new page is allocated
//   (its blocks and header, aligned to the page) and its blocks chained, the first handed out
//   (null when the allocator fails). A page left without free blocks stops being current and
//   goes to the front of the full pages.
// - engine_block_pool_free_02a3b9b0: puts the block back at the front of its page's free blocks.
//   A page that was full goes to the back of the partial pages; a page whose blocks are all free
//   again is taken off its ring (or stops being current) and handed back to the allocator. A
//   null block is ignored.
#include <cstdint>

#include "block_pool.h"

using engine::BlockPool;
using engine::BlockPoolLinks;
using engine::BlockPoolPage;
using engine::FreeBlock;

namespace {

void unlink(BlockPoolLinks *links)
{
    BlockPoolLinks *next = links->next;
    BlockPoolLinks *previous = links->previous;
    next->previous = previous;
    previous->next = next;
}

void push_front(BlockPoolLinks *ring, BlockPoolLinks *links)
{
    BlockPoolLinks *first = ring->next;
    ring->next = links;
    first->previous = links;
    links->next = first;
    links->previous = ring;
}

void push_back(BlockPoolLinks *ring, BlockPoolLinks *links)
{
    BlockPoolLinks *last = ring->previous;
    ring->previous = links;
    last->next = links;
    links->next = ring;
    links->previous = last;
}

BlockPoolPage *page_of(BlockPoolLinks *links)
{
    return reinterpret_cast<BlockPoolPage *>(links);
}

FreeBlock *block_at(uint8_t *start, uint64_t offset)
{
    return reinterpret_cast<FreeBlock *>(start + offset);
}

// A new current page: block 0 is handed out, the others chained in order (the original writes
// the chain from block 1 before it tests for the end, as here).
void *allocate_page(BlockPool *pool)
{
    const uint64_t page_size = pool->page_size;
    auto *start = static_cast<uint8_t *>(pool->allocator->allocate(page_size + sizeof(BlockPoolPage),
                                                                   0 - pool->page_mask));
    if (!start) return nullptr;
    auto *page = reinterpret_cast<BlockPoolPage *>(start + page_size);
    pool->current = page;
    page->free_count = pool->blocks_per_page - 1;
    ++pool->page_count;
    const uint64_t block_size = pool->block_size;
    page->free_blocks = block_at(start, block_size);
    uint64_t offset = block_size;
    do {
        block_at(start, offset)->next = block_at(start, offset + block_size);
        offset += block_size;
    } while (offset != page_size - block_size);
    block_at(start, page_size - block_size)->next = nullptr;
    return start;
}

}  // namespace

extern "C" void *bb_engine_block_pool_allocate_02a3b8b0(BlockPool *pool)
{
    BlockPoolPage *page = pool->current;
    if (!page) {
        BlockPoolLinks *first = pool->partial_pages.next;
        if (first == &pool->partial_pages) return allocate_page(pool);
        unlink(first);
        page = page_of(first);
        pool->current = page;
    }
    FreeBlock *block = page->free_blocks;
    page->free_blocks = block->next;
    --page->free_count;
    if (!page->free_blocks) {
        pool->current = nullptr;
        push_front(&pool->full_pages, &page->links);
    }
    return block;
}

extern "C" void bb_engine_block_pool_free_02a3b9b0(BlockPool *pool, void *data)
{
    if (!data) return;
    auto *start = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(data) & pool->page_mask);
    auto *page = reinterpret_cast<BlockPoolPage *>(start + pool->page_size);
    auto *block = static_cast<FreeBlock *>(data);
    FreeBlock *first_free = page->free_blocks;
    block->next = first_free;
    page->free_blocks = block;
    const uint64_t free_count = ++page->free_count;
    if (!first_free) {   // the page was full
        unlink(&page->links);
        push_back(&pool->partial_pages, &page->links);
        return;
    }
    if (free_count != pool->blocks_per_page) return;
    if (pool->current == page)
        pool->current = nullptr;
    else
        unlink(&page->links);
    --pool->page_count;
    pool->allocator->free(start);
}
