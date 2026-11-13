// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine's aligned allocation (pages, bins and the heap objects in
// scaleform_heap.h).
//
// engine_scaleform_heap_allocate_aligned_0233ca70 (0x0233ca70) takes the heap's engine, a request in
// bytes, an alignment, where to report the allocation and whether the caller holds the page
// manager's lock; the plain allocation (scaleform_heap_allocate.cpp) hands every request here when
// the heap's alignment is above 16. Requests up to 0x200 bytes come from the bins' aligned
// allocation (at least 16-aligned, rounded to 16) with the same page-adding retries; larger ones
// (at least 8-aligned, at least the alignment long, rounded to 8) go to the large-block allocation,
// under the page manager's lock unless the caller holds it, while it says to retry.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint64_t largest_small_request = 0x200;
constexpr uint64_t large_alignment = 8;         // the least alignment of a large block

RT_ORIGINAL(0x0233ae40, bins_allocate_aligned,
            uintptr_t(Bins *bins, uint64_t request, uint64_t alignment, PageView *view));
RT_ORIGINAL(0x0233c660, allocate_large,
            uintptr_t(Engine *engine, uint64_t request, uint64_t alignment, bool *retry, AllocationInfo *info));

uintptr_t allocate_large_retrying(Engine *engine, uint64_t bytes, uint64_t alignment, AllocationInfo *info)
{
    bool retry = false;
    for (;;) {
        if (uintptr_t block = allocate_large(engine, bytes, alignment, &retry, info))
            return block;
        if (!retry)
            return 0;
    }
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_allocate_aligned_0233ca70(Engine *engine, uint64_t request,
                                                                        uint64_t alignment, AllocationInfo *info,
                                                                        bool locked)
{
    if (request <= largest_small_request) {
        const uint64_t small_alignment = alignment >= unit_bytes ? alignment : unit_bytes;
        const uint64_t bytes = (request + (unit_bytes - 1)) & ~(unit_bytes - 1);
        return allocate_small_retrying(engine, bytes, info, locked, [&](PageView *view) {
            return bins_allocate_aligned(&engine->bins, bytes, small_alignment, view);
        });
    }
    const uint64_t block_alignment = alignment >= large_alignment ? alignment : large_alignment;
    const uint64_t length = request >= block_alignment ? request : block_alignment;
    const uint64_t bytes = (length + (large_alignment - 1)) & ~(large_alignment - 1);
    if (locked)
        return allocate_large_retrying(engine, bytes, block_alignment, info);
    void **mutex = &page_manager.get()->mutex;
    mutex_lock(mutex);
    const uintptr_t block = allocate_large_retrying(engine, bytes, block_alignment, info);
    mutex_unlock(mutex);
    return block;
}
