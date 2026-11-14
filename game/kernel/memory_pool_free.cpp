// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel: the memory pool's free (kernel_memory_pool_free_011ab3a0), the release that
// mutex_destroy.cpp and the other kernel wrappers call with the context's pool, a block and a
// source position (never read).
//
// The pool creates its lock on first use (a recursive mutex in the shared static slot; when that
// fails nothing else happens), then under the lock:
// - takes the block's size off the pool's counters: from the 8-byte header in front of the block
//   (size, flags) when the pool keeps headers, the flag 0x400000 choosing the second counter;
//   otherwise from the block's first word (page pools) or the backing heap's size query;
// - hands the block back: to the pool's free callback when it has one (header address, the
//   header flags masked by the context's flag mask), else to the page pool's page release (the
//   page index in the block's second word, the size rounded up to whole pages with a signed
//   division), else to the backing heap.
// After unlocking, a page pool repeats the whole release on the context's own pool.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;

struct BlockHeader {
    uint32_t size;
    uint32_t flags;
};
static_assert(sizeof(BlockHeader) == 0x8);

// What a page pool keeps at the start of each block it hands out.
struct PageBlock {
    uint32_t size;
    uint32_t first_page;
};

using FreeCallback = void (*)(BlockHeader *header, uint32_t flags, uint64_t reserved);

struct MemoryPool {
    uint8_t unknown_0x00[0x10];
    uint8_t is_page_pool;
    uint8_t unknown_0x11[0x0b];
    uint32_t used_bytes;
    uint8_t unknown_0x20[0x08];
    uint32_t used_secondary_bytes;
    uint8_t unknown_0x2c[0x04];
    void *keeps_headers;
    uint8_t unknown_0x38[0x08];
    FreeCallback free_callback;
    void *backing_heap;
    MutexHandle *lock;
    int32_t page_size;
};
static_assert(offsetof(MemoryPool, is_page_pool) == 0x10 && offsetof(MemoryPool, used_bytes) == 0x1c);
static_assert(offsetof(MemoryPool, used_secondary_bytes) == 0x28 && offsetof(MemoryPool, keeps_headers) == 0x30);
static_assert(offsetof(MemoryPool, free_callback) == 0x40 && offsetof(MemoryPool, backing_heap) == 0x48);
static_assert(offsetof(MemoryPool, lock) == 0x50 && offsetof(MemoryPool, page_size) == 0x58);

struct KernelContext {
    uint8_t unknown_0x00[0x08];
    MemoryPool *pool;
    uint8_t unknown_0x10[0x10];
    uint32_t callback_flag_mask;
};
static_assert(offsetof(KernelContext, pool) == 0x08 && offsetof(KernelContext, callback_flag_mask) == 0x20);

RT_ORIGINAL(0x0111abb0, kernel_mutex_create, int(MutexHandle **out, uint64_t static_storage));
RT_ORIGINAL(0x0111acd0, kernel_mutex_lock, int(MutexHandle *mutex));
RT_ORIGINAL(0x0111acf0, kernel_mutex_unlock, int(MutexHandle *mutex));
RT_ORIGINAL(0x01201f30, backing_heap_block_size, uint32_t(void *block));
RT_ORIGINAL(0x01200fc0, backing_heap_free, void(void *heap, void *block));
RT_ORIGINAL(0x011aba30, page_pool_release_pages,
            void(MemoryPool *pool, uint32_t first_page, uint64_t reserved, uint32_t page_count));
RT_GLOBAL(0x056d6ae8, kernel_context_instance, KernelContext *);

constexpr uint64_t in_static_storage = 1;
constexpr uint32_t secondary_counter_flag = 0x400000;

}  // namespace

extern "C" void bb_kernel_memory_pool_free_011ab3a0(MemoryPool *pool, void *block, const char *, int32_t)
{
    BlockHeader *header = static_cast<BlockHeader *>(block) - 1;
    for (;;) {
        if (!pool->lock && kernel_mutex_create(&pool->lock, in_static_storage) != 0) return;
        kernel_mutex_lock(pool->lock);

        uint32_t flags = 0;
        if (pool->keeps_headers) {
            flags = header->flags;
            if (flags & secondary_counter_flag)
                pool->used_secondary_bytes -= header->size;
            else
                pool->used_bytes -= header->size;
        } else if (pool->is_page_pool) {
            pool->used_bytes -= static_cast<PageBlock *>(block)->size;
        } else {
            pool->used_bytes -= backing_heap_block_size(block);
        }

        if (pool->free_callback) {
            pool->free_callback(header, flags & kernel_context_instance.get()->callback_flag_mask, 0);
        } else if (pool->is_page_pool) {
            const PageBlock *pages = static_cast<PageBlock *>(block);
            const int32_t rounded = static_cast<int32_t>(pages->size + static_cast<uint32_t>(pool->page_size) - 1);
            page_pool_release_pages(pool, pages->first_page, 0, static_cast<uint32_t>(rounded / pool->page_size));
        } else {
            backing_heap_free(pool->backing_heap, block);
        }

        kernel_mutex_unlock(pool->lock);
        if (!pool->is_page_pool) return;
        pool = kernel_context_instance.get()->pool;
    }
}
