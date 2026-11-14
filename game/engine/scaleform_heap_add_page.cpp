// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine adding a page to its bins (pages, the engine and the page
// manager in scaleform_heap.h).
//
// engine_scaleform_heap_add_page_0233bbe0 (0x0233bbe0) and engine_scaleform_heap_add_page_locked_0233bd20
// (0x0233bd20) take the engine and where to store whether its allocation should try again. When
// the engine has a limit and a limit handler, and one more page would take its pages past the
// limit, the handler is asked first (with the heap and the bytes past the limit); its answer is
// overwritten, so the call only lets it make room. The retry flag is then cleared and the page
// manager asked for a page for the engine's heap. A page found gets its location in the global page
// table (the first entry in use whose descriptor array holds it; all bits set when none does), goes
// to the bins (the bins' own function lays out its headers and bitset), is counted in the engine's
// page bytes and joins the end of the engine's ring of pages, and the retry flag is set.
//
// The first function takes the page manager's lock around getting and placing the page; the
// second is for a caller that holds it, and lets it go while the limit handler runs.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint64_t page_descriptor_shift = 5;   // sizeof(Page) == 1 << 5
static_assert(sizeof(Page) == uint64_t{1} << page_descriptor_shift);
// A descriptor's byte offset shifted this far is its index above the table entry's bits (any low
// bits of an offset that is not a whole descriptor land in the entry's bits).
constexpr uint32_t location_offset_shift = table_index_bits - page_descriptor_shift;
constexpr uint32_t no_location = ~uint32_t{0};

// The page manager's page for a heap; null when it has none.
RT_ORIGINAL(0x0233e3a0, acquire_page, Page *(PageManager *manager, MemoryHeap *heap));
// Lays out a new page's headers and bitset and gives its room to the bins.
RT_ORIGINAL(0x0233a230, bins_add_page, void(Bins *bins, Page *page, uint32_t location));

// The bytes one more page would take the engine's pages past its limit; 0 when there is no limit,
// no handler or no excess.
uint64_t excess_over_limit(const Engine *engine)
{
    if (engine->limit == 0 || engine->limit_handler == nullptr)
        return 0;
    const uint64_t wanted = engine->page_bytes_taken + page_bytes;
    return wanted > engine->limit ? wanted - engine->limit : 0;
}

bool ask_limit_handler(Engine *engine, uint64_t excess)
{
    LimitHandler *handler = engine->limit_handler;
    handler->calls_in_progress += 1;
    const bool made_room = handler->vtable->on_limit_exceeded(handler, engine->heap, excess);
    engine->limit_handler->calls_in_progress -= 1;
    return made_room;
}

// The page's location for its headers: page table entry in the low bits, index in that entry's
// descriptor array above them.
uint32_t page_location(const PageManager *manager, const Page *page)
{
    const uint32_t entries = manager->page_table_entries;
    for (uint32_t entry = 0; entry < entries; ++entry) {
        const PageTableEntry &table_entry = page_table.get()[entry];
        const int64_t offset = reinterpret_cast<intptr_t>(page) - reinterpret_cast<intptr_t>(table_entry.pages);
        // A page below the array gives a negative index, which compares as a huge one.
        if (static_cast<uint64_t>(offset >> page_descriptor_shift) <= table_entry.index_mask)
            return static_cast<uint32_t>(static_cast<uint64_t>(offset) << location_offset_shift) | entry;
    }
    return no_location;
}

// Gets a page for the engine and puts it to use (the caller holds the page manager's lock).
void take_page(Engine *engine, bool *retry)
{
    Page *page = acquire_page(page_manager.get(), engine->heap);
    if (page == nullptr)
        return;
    bins_add_page(&engine->bins, page, page_location(page_manager.get(), page));
    engine->page_bytes_taken += page_bytes;
    page->links.next = &engine->pages;
    page->links.prev = engine->pages.prev;
    engine->pages.prev->next = &page->links;
    engine->pages.prev = &page->links;
    *retry = true;
}

}  // namespace

extern "C" void bb_engine_scaleform_heap_add_page_0233bbe0(Engine *engine, bool *retry)
{
    if (const uint64_t excess = excess_over_limit(engine))
        *retry = ask_limit_handler(engine, excess);
    *retry = false;
    void **mutex = &page_manager.get()->mutex;
    mutex_lock(mutex);
    take_page(engine, retry);
    mutex_unlock(mutex);
}

extern "C" void bb_engine_scaleform_heap_add_page_locked_0233bd20(Engine *engine, bool *retry)
{
    if (const uint64_t excess = excess_over_limit(engine)) {
        void **mutex = &page_manager.get()->mutex;
        mutex_unlock(mutex);
        *retry = ask_limit_handler(engine, excess);
        mutex_lock(mutex);
    }
    *retry = false;
    take_page(engine, retry);
}
