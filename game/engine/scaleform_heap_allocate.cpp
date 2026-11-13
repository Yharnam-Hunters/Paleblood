// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine's allocation (pages, bins and the heap objects in
// scaleform_heap.h).
//
// engine_scaleform_heap_allocate_0233c870 (0x0233c870) takes the heap's engine, a request in bytes,
// where to report the allocation, and whether the caller already holds the page manager's lock;
// about 1M calls in a boot to character creation. A heap with an alignment above 16 always takes
// the aligned path. Requests up to 0x200 bytes come from the bins, rounded to 16: when the bins
// have nothing, the engine asks for a new page (with or without taking the lock) and tries again
// for as long as that says to retry. Larger requests go to the large-block allocation, under the
// page manager's lock unless the caller holds it, again while it says to retry.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint64_t largest_small_request = 0x200;

RT_ORIGINAL(0x0233a7f0, bins_allocate, FreeBlock *(Bins *bins, uint64_t request, PageView *view));
RT_ORIGINAL(0x0233c660, allocate_large,
            uintptr_t(Engine *engine, uint64_t request, uint64_t alignment, bool *retry, AllocationInfo *info));
RT_ORIGINAL(0x0233ca70, allocate_aligned,
            uintptr_t(Engine *engine, uint64_t request, uint64_t alignment, AllocationInfo *info, bool locked));

// The large-block allocation's retry loop; it reads the engine's alignment again on every try.
uintptr_t allocate_large_retrying(Engine *engine, uint64_t request, AllocationInfo *info)
{
    bool retry = false;
    for (;;) {
        if (uintptr_t block = allocate_large(engine, request, engine->alignment, &retry, info))
            return block;
        if (!retry)
            return 0;
    }
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_allocate_0233c870(Engine *engine, uint64_t request, AllocationInfo *info,
                                                                bool locked)
{
    const uint64_t alignment = engine->alignment;
    if (alignment > unit_bytes)
        return allocate_aligned(engine, request, alignment, info, locked);
    if (request <= largest_small_request) {
        const uint64_t bytes = (request + (unit_bytes - 1)) & ~(unit_bytes - 1);
        return allocate_small_retrying(engine, bytes, info, locked, [&](PageView *view) {
            return reinterpret_cast<uintptr_t>(bins_allocate(&engine->bins, bytes, view));
        });
    }
    if (locked)
        return allocate_large_retrying(engine, request, info);
    void **mutex = &page_manager.get()->mutex;
    mutex_lock(mutex);
    const uintptr_t block = allocate_large_retrying(engine, request, info);
    mutex_unlock(mutex);
    return block;
}
