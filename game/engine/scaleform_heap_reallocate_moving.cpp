// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine's moving reallocation of a small block (pages, bins and the
// heap objects in scaleform_heap.h).
//
// engine_scaleform_heap_reallocate_small_moving_0233d140 (0x0233d140) takes the heap's engine, the
// block's page, the block, the new length in bytes, where to report the new allocation, and whether
// the caller holds the page manager's lock (its low byte is passed on to the allocation and the
// page release). It allocates the new length from the engine (scaleform_heap_allocate.cpp) and
// returns 0 when that fails, leaving the old block alone. Otherwise it copies the shorter of the old block's length (read from its page's bitset)
// and the length the allocation reports, then frees the old block the way the engine's free does
// (scaleform_heap_free.cpp): the bins take it back, the engine's used bytes drop by its length,
// the page's header counts drop and an emptied page is released; and returns the new block.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

RT_ORIGINAL(0x0233c870, allocate, uintptr_t(Engine *engine, uint64_t request, AllocationInfo *info, uint32_t locked));
RT_ORIGINAL(0x02fbe578, copy_bytes, void *(void *destination, const void *source, uint64_t bytes));
RT_ORIGINAL(0x0233ab20, bins_free,
            void(Bins *bins, Page *page, uintptr_t block, PageView *view, uint64_t *freed_bytes));
RT_ORIGINAL(0x0233be70, release_page, void(Engine *engine, Page *page, uint32_t locked));

// The length in bytes of the allocated block at `block` in its page.
uint64_t allocated_bytes(Page *page, uintptr_t block)
{
    const PageView view = describe_page(page);
    return uint64_t{bitset_allocated_units(view.bitset, unit_in_page(view, block))} << unit_shift;
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_reallocate_small_moving_0233d140(Engine *engine, Page *page,
                                                                               uintptr_t block, uint64_t bytes,
                                                                               AllocationInfo *info, int32_t locked)
{
    // The flag goes on as its low byte, widened.
    const uint32_t holds_lock = static_cast<uint8_t>(locked);
    const uintptr_t moved = allocate(engine, bytes, info, holds_lock);
    if (moved == 0)
        return 0;
    const uint64_t old_bytes = allocated_bytes(page, block);
    const uint64_t copied = old_bytes < info->bytes ? old_bytes : info->bytes;
    copy_bytes(reinterpret_cast<void *>(moved), reinterpret_cast<const void *>(block), copied);

    PageView view;
    uint64_t freed_bytes;
    bins_free(&engine->bins, page, block, &view, &freed_bytes);
    engine->used_bytes -= freed_bytes;
    // Only the last header's count decides (the upper one when the page has both).
    uint16_t remaining = 0;
    for (PageHeader *header : {view.header_below, view.header_above}) {
        if (header)
            remaining = --header->block_count;
    }
    if (remaining == 0)
        release_page(engine, page, holds_lock);
    engine->allocation_count -= 1;
    return moved;
}
