// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's reallocation (pages, the heap objects and the large-block tree in
// scaleform_heap.h).
//
// engine_scaleform_heap_reallocate_023392d0 (0x023392d0, the global heap's vtable slot 12) takes
// the heap, a block and the new length in bytes, and returns the block's new address (0 when it
// failed). The block's owner decides the path:
//   - a block inside a small-block page: under 0x800 bytes, the engine first resizes it where it is
//     (rounded to 16, adjusting the engine's used bytes); otherwise, or when that fails, the moving
//     reallocation runs under the page manager's lock;
//   - any other block is a large block: the page manager's tree (searched under its lock) gives the
//     block's node, the lowest at or above the address, and its heap; the large-block reallocation
//     then runs under the page manager's lock, with the length rounded to 8.
// A thread-safe heap's own lock is held around all of it, taken before the page manager's.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint64_t in_place_limit = 0x800;      // longer requests never resize in place
constexpr uint64_t large_granularity = 8;
constexpr uint64_t key_top_bit = 63;

RT_ORIGINAL(0x0233b2f0, bins_reallocate,
            uintptr_t(Bins *bins, Page *page, uintptr_t block, uint64_t bytes, uint64_t *old_bytes, PageView *view));
// Moves a small block (allocate, copy, free); the last argument says the caller holds the page
// manager's lock.
RT_ORIGINAL(0x0233d140, reallocate_small_moving,
            uintptr_t(Engine *engine, Page *page, uintptr_t block, uint64_t bytes, AllocationInfo *info, int32_t locked));
RT_ORIGINAL(0x0233ccf0, reallocate_large,
            uintptr_t(Engine *engine, LargeBlockNode *node, uintptr_t block, uint64_t bytes, AllocationInfo *info));

void **page_manager_mutex()
{
    return &page_manager.get()->mutex;
}

// The engine resizes the block where it is; 0 when it cannot.
uintptr_t resize_in_place(MemoryHeap *heap, Page *page, uintptr_t block, uint64_t size)
{
    if (size >= in_place_limit)
        return 0;
    Engine *engine = heap->engine;
    const uint64_t bytes = (size + (unit_bytes - 1)) & ~(unit_bytes - 1);
    uint64_t old_bytes;
    PageView view;
    const uintptr_t resized = bins_reallocate(&engine->bins, page, block, bytes, &old_bytes, &view);
    if (resized)
        engine->used_bytes += bytes - old_bytes;
    return resized;
}

uintptr_t reallocate_small(MemoryHeap *heap, Page *page, uintptr_t block, uint64_t size)
{
    if (uintptr_t resized = resize_in_place(heap, page, block, size))
        return resized;
    AllocationInfo info;
    void **mutex = page_manager_mutex();
    mutex_lock(mutex);
    const uintptr_t moved = reallocate_small_moving(heap->engine, page, block, size, &info, 1);
    mutex_unlock(mutex);
    return moved;
}

// The node with the lowest address at or above `key`. The walk follows the key's bits from the
// top and remembers the last right branch it passed; when no node on the path is the key itself,
// the leftmost path below that branch holds the remaining candidates. Null (which the caller then
// reads through) when the tree is empty or holds nothing at or above the key.
LargeBlockNode *find_large_block(uintptr_t key)
{
    LargeBlockNode *best = nullptr;
    uint64_t best_distance = ~uint64_t{0};
    auto consider = [&](LargeBlockNode *node) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(node);
        if (address >= key && address - key < best_distance) {
            best = node;
            best_distance = address - key;
        }
    };
    LargeBlockNode *right_branch = nullptr;
    uint64_t bits = key;
    for (LargeBlockNode *node = page_manager.get()->root; node;) {
        consider(node);
        if (reinterpret_cast<uintptr_t>(node) == key)
            return node;
        LargeBlockNode *right = node->children[1];
        LargeBlockNode *next = node->children[bits >> key_top_bit];
        if (right && right != next)
            right_branch = right;
        bits <<= 1;
        node = next;
    }
    for (LargeBlockNode *node = right_branch; node; node = node->children[node->children[0] == nullptr])
        consider(node);
    return best;
}

uintptr_t reallocate_large_block(uintptr_t block, uint64_t size)
{
    void **mutex = page_manager_mutex();
    mutex_lock(mutex);
    LargeBlockNode *node = find_large_block(block);
    auto *heap = reinterpret_cast<MemoryHeap *>(node->heap_and_flags & ~node_flag_bits);
    mutex_unlock(mutex);

    const uint64_t bytes = (size + (large_granularity - 1)) & ~(large_granularity - 1);
    AllocationInfo info;
    const bool thread_safe = heap->thread_safe;
    if (thread_safe)
        mutex_lock(&heap->mutex);
    mutex = page_manager_mutex();
    mutex_lock(mutex);
    const uintptr_t moved = reallocate_large(heap->engine, node, block, bytes, &info);
    mutex_unlock(mutex);
    if (thread_safe)
        mutex_unlock(&heap->mutex);
    return moved;
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_reallocate_023392d0(void *, uintptr_t block, uint64_t size)
{
    Page *page = find_page(block);
    if (!page)
        return reallocate_large_block(block, size);
    MemoryHeap *heap = page->heap;
    if (!heap->thread_safe)
        return reallocate_small(heap, page, block, size);
    mutex_lock(&heap->mutex);
    const uintptr_t result = reallocate_small(heap, page, block, size);
    mutex_unlock(&heap->mutex);
    return result;
}
