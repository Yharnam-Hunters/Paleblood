// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine's small-block free (pages, bins and the heap objects in
// scaleform_heap.h).
//
// engine_scaleform_heap_free_0233c0a0 (0x0233c0a0) takes the heap's engine, the block's page, the
// block and a flag it passes on to the page release. The bins take the block back
// (scaleform_heap_bin_free.cpp), the engine's used bytes drop by its length, and the block leaves
// the counts in the page's headers; when the last count it decremented reaches zero (or the page
// has neither header), the page is released.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

RT_ORIGINAL(0x0233ab20, bins_free,
            void(Bins *bins, Page *page, uintptr_t block, PageView *view, uint64_t *freed_bytes));
// Gives an empty page back to the page source.
RT_ORIGINAL(0x0233be70, release_page, void(Engine *engine, Page *page, bool flag));

}  // namespace

extern "C" void bb_engine_scaleform_heap_free_0233c0a0(Engine *engine, Page *page, uintptr_t block, bool flag)
{
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
        release_page(engine, page, flag);
    engine->allocation_count -= 1;
}
