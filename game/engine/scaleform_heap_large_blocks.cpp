// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap engine's large blocks and its page release (pages, the engine and the
// page manager in scaleform_heap.h).
//
// engine_scaleform_heap_allocate_large_0233c660 (0x0233c660) takes the engine, a request in bytes,
// an alignment, where to store whether the allocation should try again and where to report the
// allocation; the engine's allocations call it under the page manager's lock for requests above
// 0x200 bytes. The request is rounded up to 8 and a node for the page manager's tree of large
// blocks goes right after it (with one more word for the alignment when that is above 16). When
// the engine has a limit and the block would take its pages past it, the limit handler (if any) is
// asked first, with the page manager's lock let go around the call; its answer is overwritten. The
// retry flag is cleared and the heap's arena allocator asked for the bytes. A block that comes back
// gets its node: the heap with flags for the alignment (1, 2 or 4: none; 8: 1; 16: 2; anything else:
// 3, with the alignment stored after the node), cleared links, and a place in the tree. The
// allocation is reported (no page, the node, the rounded length) and counted in the engine, and the
// retry flag is set.
//
// engine_scaleform_heap_release_page_0233be70 (0x0233be70) takes the engine, an empty page and
// whether the caller holds the page manager's lock (its low byte; the lock is taken around the
// release otherwise). The page's room leaves the bins, the page leaves the engine's ring, its
// headers are cleared, its memory goes back to the page manager's allocator and its descriptor to
// the front of the page manager's spare descriptors; the engine's page bytes drop by a page.
#include <cstdint>
#include <cstring>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

// The system allocators the engine and the page manager take memory from.
struct SystemAllocator;
struct SystemAllocatorVtable {
    void *unknown_slots_0[5];
    void (*free)(SystemAllocator *allocator, uintptr_t memory, uint64_t bytes, uint64_t alignment);
    void *unknown_slots_6[5];
    uintptr_t (*allocate)(SystemAllocator *allocator, uint64_t bytes, uint64_t alignment);
};
static_assert(offsetof(SystemAllocatorVtable, free) == 0x28 && offsetof(SystemAllocatorVtable, allocate) == 0x58);
struct SystemAllocator {
    const SystemAllocatorVtable *vtable;
};

// Fields of the shared structs that scaleform_heap.h leaves unnamed.
constexpr std::size_t engine_root_offset = 0x0;     // Engine: the heap root
constexpr std::size_t root_arenas_offset = 0x10;    // heap root: the arenas' allocators
constexpr std::size_t heap_arena_offset = 0xb8;     // MemoryHeap: its arena (int32)
constexpr std::size_t manager_allocator_offset = 0x0; // PageManager: its allocator
constexpr std::size_t manager_spares_offset = 0x10; // PageManager: the ring of spare descriptors
constexpr std::size_t node_parent_offset = 0x0;     // LargeBlockNode: its parent (the root's: the tree's root slot)

template <typename T>
T read_at(const void *object, std::size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof value);
    return value;
}

template <typename T>
void write_at(void *object, std::size_t offset, T value)
{
    std::memcpy(static_cast<uint8_t *>(object) + offset, &value, sizeof value);
}

// A large block's node goes right after the block; flag 3 keeps the alignment in the word after it.
constexpr uint64_t large_rounding = 8;
constexpr uint64_t alignment_word_bytes = 8;
constexpr uint64_t node_bytes = sizeof(LargeBlockNode);
static_assert(node_bytes == 0x20);
constexpr uint64_t largest_flagged_alignment = 16;  // above it, the alignment word is reserved

// The node's flags (the low bits of heap_and_flags) for the block's alignment.
constexpr uintptr_t small_alignment_flags = 0;      // 1, 2 or 4
constexpr uintptr_t alignment_8_flags = 1;
constexpr uintptr_t alignment_16_flags = 2;
constexpr uintptr_t stored_alignment_flags = 3;     // the alignment is in the word after the node
constexpr uint64_t byte_alignment = 1, short_alignment = 2, int_alignment = 4, long_alignment = 8;

constexpr uint64_t page_alignment = 8;              // what the page's memory is freed with

// Takes a page's room out of the bins (the inverse of the page add's bins function).
RT_ORIGINAL(0x0233a630, bins_remove_page, void(Bins *bins, uintptr_t base));

SystemAllocator *arena_allocator(const Engine *engine)
{
    const auto *root = read_at<const uint8_t *>(engine, engine_root_offset);
    const auto arena = read_at<int32_t>(engine->heap, heap_arena_offset);
    return read_at<SystemAllocator *>(root, root_arenas_offset + static_cast<std::size_t>(arena) * sizeof(void *));
}

// Asks the limit handler to make room, with the page manager's lock let go around the call.
bool ask_limit_handler(Engine *engine, uint64_t bytes)
{
    void **mutex = &page_manager.get()->mutex;
    mutex_unlock(mutex);
    LimitHandler *handler = engine->limit_handler;
    handler->calls_in_progress += 1;
    const uint64_t excess = engine->page_bytes_taken + bytes - engine->limit;
    const bool made_room = handler->vtable->on_limit_exceeded(handler, engine->heap, excess);
    engine->limit_handler->calls_in_progress -= 1;
    mutex_lock(mutex);
    return made_room;
}

void set_parent(LargeBlockNode *node, uintptr_t parent)
{
    write_at(node, node_parent_offset, parent);
}

// Puts a node in the page manager's tree, keyed by its address: each level goes left or right by
// the key's next bit from the top. A node already in the tree stays where it is.
void insert_node(PageManager *manager, LargeBlockNode *node)
{
    if (manager->root == nullptr) {
        manager->root = node;
        set_parent(node, reinterpret_cast<uintptr_t>(&manager->root));
        return;
    }
    constexpr unsigned key_top_bit = 63;
    uintptr_t key = reinterpret_cast<uintptr_t>(node);
    for (LargeBlockNode *at = manager->root; at != node;) {
        LargeBlockNode *&child = at->children[key >> key_top_bit];
        key <<= 1;
        if (child == nullptr) {
            child = node;
            set_parent(node, reinterpret_cast<uintptr_t>(at));
            return;
        }
        at = child;
    }
}

uintptr_t flags_for(uint64_t alignment)
{
    if (alignment == byte_alignment || alignment == short_alignment || alignment == int_alignment)
        return small_alignment_flags;
    if (alignment == long_alignment)
        return alignment_8_flags;
    if (alignment == largest_flagged_alignment)
        return alignment_16_flags;
    return stored_alignment_flags;
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_allocate_large_0233c660(Engine *engine, uint64_t request,
                                                                      uint64_t alignment, bool *retry,
                                                                      AllocationInfo *info)
{
    const uint64_t length = (request + (large_rounding - 1)) & ~(large_rounding - 1);
    const uint64_t extra = alignment > largest_flagged_alignment ? alignment_word_bytes : 0;
    const uint64_t bytes = length + node_bytes + extra;
    if (engine->limit != 0 && engine->page_bytes_taken + bytes > engine->limit && engine->limit_handler)
        *retry = ask_limit_handler(engine, bytes);
    *retry = false;

    SystemAllocator *allocator = arena_allocator(engine);
    const uintptr_t block = allocator->vtable->allocate(allocator, bytes, alignment);
    if (block == 0)
        return 0;

    PageManager *manager = page_manager.get();
    auto *node = reinterpret_cast<LargeBlockNode *>(block + length);
    const uintptr_t flags = flags_for(alignment);
    node->heap_and_flags = reinterpret_cast<uintptr_t>(engine->heap) | flags;
    if (flags == stored_alignment_flags)
        write_at(node, node_bytes, alignment);
    node->children[1] = nullptr;
    node->children[0] = nullptr;
    set_parent(node, 0);
    insert_node(manager, node);

    info->bytes = length;
    info->page = nullptr;
    info->unknown_0x08 = reinterpret_cast<uintptr_t>(node);
    engine->allocation_count += 1;
    engine->page_bytes_taken += bytes;
    engine->used_bytes += length;
    *retry = true;
    return block;
}

namespace {

// The release itself (the caller holds the page manager's lock).
void release_page_held(Engine *engine, Page *page)
{
    bins_remove_page(&engine->bins, page->base);
    page->links.next->prev = page->links.prev;
    page->links.prev->next = page->links.next;

    PageManager *manager = page_manager.get();
    const PageView view = describe_page(page);
    for (PageHeader *header : {view.header_below, view.header_above}) {
        if (header)
            std::memset(header, 0, header_bytes);
    }
    const uintptr_t base = page->base;
    page->base = 0;
    page->heap = nullptr;
    auto *allocator = read_at<SystemAllocator *>(manager, manager_allocator_offset);
    allocator->vtable->free(allocator, base, page_bytes, page_alignment);

    auto *spares = reinterpret_cast<PageLinks *>(reinterpret_cast<uint8_t *>(manager) + manager_spares_offset);
    page->links.next = spares->next;
    page->links.prev = spares;
    spares->next->prev = &page->links;
    spares->next = &page->links;
}

}  // namespace

extern "C" void bb_engine_scaleform_heap_release_page_0233be70(Engine *engine, Page *page, uint64_t locked)
{
    if (static_cast<uint8_t>(locked) != 0) {
        release_page_held(engine, page);
    } else {
        void **mutex = &page_manager.get()->mutex;
        mutex_lock(mutex);
        release_page_held(engine, page);
        mutex_unlock(mutex);
    }
    engine->page_bytes_taken -= page_bytes;
}
