// SPDX-License-Identifier: GPL-2.0-or-later
// The Scaleform memory heap the game's user interface allocates from (Scaleform 4's page-table
// heap, MemoryHeapPT): its small-block engine, which carves blocks of up to 0x200 bytes out of
// 4 KiB pages.
//
// A page is 4 KiB of memory that need not be page-aligned: its start and end are its base rounded
// up to 16 bytes, and the machine page boundary inside it holds the page's headers, one in the 16
// bytes below the boundary and one in the 16 above, each where there is room. A header carries a
// signature and where the page's descriptor is in the global page table, so any address inside the
// page leads to its descriptor: the header at the start of the address's machine page, else the one
// in that page's last 16 bytes. Beside the headers sits the page's bitset: two bits per 16-byte unit
// of the page, zero for a free unit, which also encode each allocated block's length (the bitset's
// own functions, which the engine calls, write and read that code).
//
// Free blocks of the engine are in 64 bins: circular doubly linked lists, bin n holding blocks of
// n + 1 units (the last bin every larger one), with one bit per bin in the bins' mask while its list
// is not empty. A free block's first two words are its links; their low four bits are not part of
// the link but hold the block's length in units (low nibble in the first word, high nibble in the
// second), and its last 16 bytes repeat the two words. A free block of three or more units also
// points at its page's descriptor, so freeing it next to another does not need the lookup.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "runtime/original.h"

namespace engine::scaleform_heap {

constexpr uint64_t unit_bytes = 16;
constexpr uint64_t unit_shift = 4;              // bytes to units
constexpr uint64_t length_nibble = 0xf;         // the length bits in a link word
constexpr uint64_t link_mask = ~length_nibble;
constexpr uint64_t length_nibble_bits = 4;
constexpr uint64_t word_alignment_mask = 7;
constexpr uint64_t bin_count = 64;
constexpr uint64_t last_bin = bin_count - 1;
constexpr uint64_t bin_shift_mask = 63;         // the bin bit's shift uses the low six bits
constexpr uint64_t units_with_page = 3;         // free blocks this long point at their page

constexpr uint64_t page_bytes = 0x1000;
constexpr uintptr_t machine_page_mask = ~uintptr_t{page_bytes - 1};
constexpr uintptr_t last_header_offset = 0xff0; // the header in a machine page's last 16 bytes
constexpr uint64_t header_bytes = 16;
constexpr uint64_t bitset_bytes = 0x40;         // two bits for each of the page's 256 units
constexpr uint16_t page_signature = 0x5fc0;
constexpr uint32_t table_index_bits = 7;
constexpr uint32_t table_index_mask = (1u << table_index_bits) - 1;
constexpr uint64_t page_table_size = uint64_t{1} << table_index_bits;

constexpr uint64_t unit_code_bits = 2;          // bitset code width per unit
constexpr uint32_t unit_code_mask = 3;
constexpr uint64_t units_per_bitset_word = 16;
constexpr uint64_t bitset_word_shift = 4;       // units to bitset words
constexpr uint64_t unit_code_shift_mask = 0x1e; // shifts stay inside the 32-bit word
constexpr uint64_t long_length_offset = 0x25;   // see long_length_word
constexpr uint64_t long_length_shift = 5;
// A block's length code takes up to its first five units (scaleform_heap_bitset_mark.cpp).
constexpr uint64_t second_code_unit = 1;
constexpr uint64_t third_code_unit = 2;
constexpr uint64_t fourth_code_unit = 3;
constexpr uint64_t fifth_code_unit = 4;

// The page's descriptor (0x20 bytes, in the arrays the page table points at).
struct MemoryHeap;
// A link of an engine's ring of pages (the engine holds the ring's own node).
struct PageLinks {
    PageLinks *next;
    PageLinks *prev;
};
struct Page {
    PageLinks links;            // in the ring of its engine's pages
    MemoryHeap *heap;           // the heap the page belongs to
    uintptr_t base;             // where the page's 4 KiB start, before rounding to 16 bytes
};
static_assert(offsetof(Page, heap) == 0x10 && offsetof(Page, base) == 0x18 && sizeof(Page) == 0x20);

// One of the two headers at the page's machine page boundary.
struct PageHeader {
    uint16_t signature;         // page_signature
    uint16_t block_count;       // blocks handed out from the page (the allocation counts them)
    uint32_t location;          // low table_index_bits: page table entry; the rest: index in it
};
static_assert(sizeof(PageHeader) == 8);

struct PageTableEntry {
    Page *pages;
    uint64_t index_mask;        // applied to the header's index
};
RT_GLOBAL(0x05994470, page_table, PageTableEntry[page_table_size]);

// A free block's first words; the last 16 bytes of the block repeat next_word and prev_word.
struct FreeBlock {
    uint64_t next_word;         // link to the next block in the bin, low nibble: length bits 0-3
    uint64_t prev_word;         // link to the previous block, low nibble: length bits 4-7
    Page *page;                 // only in blocks of units_with_page units or more
};
static_assert(offsetof(FreeBlock, page) == 0x10);

struct Bins {
    uint64_t mask;              // bit n: bin n is not empty
    FreeBlock *heads[bin_count];
};
static_assert(offsetof(Bins, heads) == 8);

// What the engine works out about a block's page (computed again by every operation, and handed
// back to callers, which count blocks in the headers).
struct PageView {
    PageHeader *header_below;   // the header below the boundary, null when the page has no room there
    PageHeader *header_above;
    uint32_t *bitset;
    uintptr_t start;            // base rounded up to 16
    uintptr_t end;              // base + page_bytes rounded down to 16
    uintptr_t boundary;         // the machine page boundary inside the page
    Page *page;
};
static_assert(sizeof(PageView) == 0x38);

// What an engine asks when a new page would take its pages past their limit: slot 2 of the
// handler's vtable, with the heap and the bytes past the limit (true: the handler made room). The
// engine counts the calls in progress around it.
struct LimitHandler;
struct LimitHandlerVtable {
    void *unknown_slots[2];
    bool (*on_limit_exceeded)(LimitHandler *handler, MemoryHeap *heap, uint64_t excess);
};
struct LimitHandler {
    const LimitHandlerVtable *vtable;
    int32_t calls_in_progress;
};
static_assert(offsetof(LimitHandlerVtable, on_limit_exceeded) == 0x10);
static_assert(offsetof(LimitHandler, calls_in_progress) == 8);

// The small-block engine (part of the heap's allocation engine).
struct Engine {
    uint8_t unknown_0x00[0x8];
    MemoryHeap *heap;           // the heap the engine allocates for
    uint64_t alignment;         // the heap's minimum alignment: above 16, every allocation is aligned
    Bins bins;
    PageLinks pages;            // the ring of the engine's pages
    uint64_t page_bytes_taken;  // page_bytes for each page in the ring
    uint64_t used_bytes;        // bytes handed out
    uint64_t limit;             // 0: no limit on page_bytes_taken
    LimitHandler *limit_handler;
    uint64_t allocation_count;
};
static_assert(offsetof(Engine, heap) == 8 && offsetof(Engine, bins) == 0x18 && offsetof(Engine, pages) == 0x220);
static_assert(offsetof(Engine, page_bytes_taken) == 0x230 && offsetof(Engine, used_bytes) == 0x238);
static_assert(offsetof(Engine, limit) == 0x240 && offsetof(Engine, limit_handler) == 0x248);
static_assert(offsetof(Engine, allocation_count) == 0x250);

// The heap object, as far as the engine's callers read it.
struct MemoryHeap {
    uint8_t unknown_0x00[0x98];
    void *mutex;                // the heap's own lock (a pthread mutex handle)
    bool thread_safe;           // lock the heap's mutex around its operations
    uint8_t unknown_0xa1[0x7];
    Engine *engine;
};
static_assert(offsetof(MemoryHeap, mutex) == 0x98 && offsetof(MemoryHeap, thread_safe) == 0xa0);
static_assert(offsetof(MemoryHeap, engine) == 0xa8);

// What a heap allocation reports to its caller.
struct AllocationInfo {
    Page *page;                 // the small block's page
    uint64_t unknown_0x08;      // 0 for a small block
    uint64_t bytes;             // the length handed out
};
static_assert(sizeof(AllocationInfo) == 0x18);

// The global page manager (the heaps' shared page source): its lock, taken around every operation
// that may take pages or touch the large-block tree, and the tree's root.
struct LargeBlockNode;
struct PageManager {
    uint8_t unknown_0x00[0x8];
    void *mutex;
    uint8_t unknown_0x10[0x10];
    uint32_t page_table_entries; // entries of the global page table in use
    uint8_t unknown_0x24[0x4];
    LargeBlockNode *root;
};
static_assert(offsetof(PageManager, mutex) == 0x8 && offsetof(PageManager, page_table_entries) == 0x20);
static_assert(offsetof(PageManager, root) == 0x28);
RT_GLOBAL(0x05940360, page_manager, PageManager *);

RT_ORIGINAL(0x02fbe598, mutex_lock, int(void **mutex));
RT_ORIGINAL(0x02fbe5a8, mutex_unlock, int(void **mutex));

// A large block's node in the page manager's tree of large blocks, keyed by the node's own
// address (a radix tree: each level goes left or right by the next bit of the key).
constexpr uint64_t tree_branches = 2;
struct LargeBlockNode {
    uint8_t unknown_0x00[0x8];
    LargeBlockNode *children[tree_branches];
    uintptr_t heap_and_flags;   // the owning MemoryHeap, low two bits: flags
};
static_assert(offsetof(LargeBlockNode, children) == 0x8 && offsetof(LargeBlockNode, heap_and_flags) == 0x18);
constexpr uintptr_t node_flag_bits = 3;

// The bitset's own functions: mark `units` units from `unit` as one allocated block, and read the
// length of the allocated block that starts at `unit` (scaleform_heap_bitset_mark.cpp,
// scaleform_heap_bitset_units.cpp).
RT_ORIGINAL(0x027ec640, bitset_mark_allocated, void(uint32_t *bitset, uint64_t unit, uint64_t units));
RT_ORIGINAL(0x027ec890, bitset_allocated_units, uint32_t(const uint32_t *bitset, uint64_t unit));

// The page descriptor a header names, if the header is one and `address` is inside that page.
inline Page *page_from_header(const PageHeader *header, uintptr_t address)
{
    if (header->signature != page_signature)
        return nullptr;
    const PageTableEntry &entry = page_table.get()[header->location & table_index_mask];
    Page *page = &entry.pages[(header->location >> table_index_bits) & entry.index_mask];
    if (address - page->base >= page_bytes)
        return nullptr;
    return page;
}

// The page descriptor of an address, or null: the header at the start of its machine page first,
// then the one in that machine page's last 16 bytes.
inline Page *find_page(uintptr_t address)
{
    const uintptr_t machine_page = address & machine_page_mask;
    if (Page *page = page_from_header(reinterpret_cast<const PageHeader *>(machine_page), address))
        return page;
    return page_from_header(reinterpret_cast<const PageHeader *>(machine_page | last_header_offset), address);
}

inline uint64_t link_target(uint64_t word)
{
    return word & link_mask;
}

inline FreeBlock *link_block(uint64_t word)
{
    return reinterpret_cast<FreeBlock *>(link_target(word));
}

inline uint64_t with_link(uint64_t word, uint64_t target)
{
    return (word & length_nibble) | target;
}

inline uint64_t units_of(const FreeBlock *block)
{
    return ((block->prev_word & length_nibble) << length_nibble_bits) | (block->next_word & length_nibble);
}

inline uint64_t bin_of(uint64_t units)
{
    const uint64_t index = units - 1;           // 0 units wraps to the last bin
    return index < last_bin ? index : last_bin;
}

inline uint64_t bin_bit(uint64_t bin)
{
    return uint64_t{1} << (bin & bin_shift_mask);
}

// The lowest set bit's index in a non-empty bin mask.
inline uint64_t lowest_bin(uint64_t mask)
{
    return static_cast<uint64_t>(__builtin_ctzll(mask));
}

// The page of a free block: the pointer it carries when it is long enough, else the lookup (null
// when the lookup fails; the callers then read through it).
inline Page *page_of_free_block(const FreeBlock *block)
{
    if (units_of(block) >= units_with_page && block->page)
        return block->page;
    return find_page(reinterpret_cast<uintptr_t>(block));
}

inline PageView describe_page(Page *page)
{
    PageView view{};
    const uintptr_t base = page->base;
    view.start = (base + (unit_bytes - 1)) & ~(unit_bytes - 1);
    view.end = (base + page_bytes) & ~(unit_bytes - 1);
    view.boundary = (base + (page_bytes - 1)) & machine_page_mask;
    const uint64_t below = view.boundary - view.start;
    const uint64_t above = view.end - view.boundary;
    if (below > header_bytes)
        view.header_below = reinterpret_cast<PageHeader *>(view.boundary - header_bytes);
    if (above > header_bytes)
        view.header_above = reinterpret_cast<PageHeader *>(view.boundary);
    // The bitset goes on the side with more room: below the lower header, or above the upper one.
    const uintptr_t bitset = below > above ? view.boundary - header_bytes - bitset_bytes : view.boundary | header_bytes;
    view.bitset = reinterpret_cast<uint32_t *>(bitset);
    view.page = page;
    return view;
}

// The unit of the page an address is in (arithmetic shift, as the game's).
inline uint64_t unit_in_page(const PageView &view, uintptr_t address)
{
    return static_cast<uint64_t>(static_cast<int64_t>(address - view.start) >> unit_shift);
}

inline uint32_t unit_code_bits_at(uint64_t unit)
{
    return unit_code_mask << ((unit * unit_code_bits) & unit_code_shift_mask);
}

inline uint32_t unit_code(const uint32_t *bitset, uint64_t unit)
{
    return (bitset[unit >> bitset_word_shift] >> ((unit * unit_code_bits) & unit_code_shift_mask)) & unit_code_mask;
}

// Stores a unit's code (a value wider than two bits spills into the next unit's, as the game's
// stores do; the marking never passes one).
inline void set_unit_code(uint32_t *bitset, uint64_t unit, uint32_t code)
{
    const uint64_t shift = (unit * unit_code_bits) & unit_code_shift_mask;
    uint32_t &word = bitset[unit >> bitset_word_shift];
    word = (word & ~(unit_code_mask << shift)) | (code << shift);
}

// The word a block longer than 0x25 units keeps its length in (past its first three units).
inline uint64_t long_length_word(uint64_t unit)
{
    return (unit * unit_code_bits + long_length_offset) >> long_length_shift;
}

inline bool unit_is_free(const uint32_t *bitset, uint64_t unit)
{
    return (bitset[unit >> bitset_word_shift] & unit_code_bits_at(unit)) == 0;
}

inline void clear_unit(uint32_t *bitset, uint64_t unit)
{
    bitset[unit >> bitset_word_shift] &= ~unit_code_bits_at(unit);
}

// Marks a free block's first and last unit free in the bitset (the units between keep whatever
// code they had; only the ends are read).
inline void clear_free_ends(const PageView &view, uintptr_t block, uint64_t units)
{
    const uint64_t unit = unit_in_page(view, block);
    clear_unit(view.bitset, unit);
    clear_unit(view.bitset, units + unit - 1);
}

// Writes a free block's length (bytes, a multiple of 16) into its link words and its last 16 bytes,
// keeping the links.
inline void write_free_length(FreeBlock *block, uint64_t bytes)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    // The copy goes at the length rounded down to 8 (the same for the multiples of 16 callers pass).
    auto *tail = reinterpret_cast<FreeBlock *>(address + (bytes & ~word_alignment_mask) - header_bytes);
    const uint64_t next_word = ((bytes >> unit_shift) & length_nibble) | (block->next_word & link_mask);
    block->next_word = next_word;
    tail->next_word = next_word;
    const uint64_t prev_word = (bytes >> (unit_shift + length_nibble_bits)) | (block->prev_word & link_mask);
    block->prev_word = prev_word;
    tail->prev_word = prev_word;
}

// Writes a free block's length in units into its link words and its last unit (the merge's form:
// the high nibble's word takes every bit above the low four).
inline void write_free_units(FreeBlock *block, uint64_t units)
{
    auto *tail = reinterpret_cast<FreeBlock *>(reinterpret_cast<uintptr_t>(block) + units * unit_bytes - header_bytes);
    const uint64_t next_word = (units & length_nibble) | (block->next_word & link_mask);
    block->next_word = next_word;
    tail->next_word = next_word;
    const uint64_t prev_word = (units >> length_nibble_bits) | (block->prev_word & link_mask);
    block->prev_word = prev_word;
    tail->prev_word = prev_word;
}

// Puts a free block at the head of a bin.
inline void push_to_bin(Bins *bins, FreeBlock *block, uint64_t bin)
{
    FreeBlock *head = bins->heads[bin];
    const uint64_t self = reinterpret_cast<uintptr_t>(block);
    if (head) {
        block->next_word = with_link(block->next_word, reinterpret_cast<uintptr_t>(head));
        block->prev_word = with_link(block->prev_word, link_target(head->prev_word));
        FreeBlock *last = link_block(head->prev_word);
        last->next_word = with_link(last->next_word, self);
        head->prev_word = with_link(head->prev_word, self);
    } else {
        block->next_word = with_link(block->next_word, self);
        block->prev_word = with_link(block->prev_word, self);
    }
    bins->heads[bin] = block;
    bins->mask |= bin_bit(bin);
}

// Puts a free block (its length already written) at the head of its bin.
inline void push_free(Bins *bins, FreeBlock *block)
{
    push_to_bin(bins, block, bin_of(units_of(block)));
}

// Makes `bytes` at `address` a free block of the page: length, page pointer, bin, and the bitset
// ends.
inline void release_range(Bins *bins, const PageView &view, uintptr_t address, uint64_t bytes)
{
    auto *block = reinterpret_cast<FreeBlock *>(address);
    write_free_length(block, bytes);
    if (units_of(block) >= units_with_page)
        block->page = view.page;
    push_free(bins, block);
}

// Takes a free block out of a bin. The bin's head moves to the block's previous one, not its next
// (the order Scaleform's bins keep).
inline void unlink_from_bin(Bins *bins, FreeBlock *block, uint64_t bin)
{
    const uint64_t self = reinterpret_cast<uintptr_t>(block);
    if (reinterpret_cast<uintptr_t>(bins->heads[bin]) == self) {
        const uint64_t previous = link_target(block->prev_word);
        if (previous == self) {
            bins->heads[bin] = nullptr;
            bins->mask &= ~bin_bit(bin);
            return;
        }
        bins->heads[bin] = reinterpret_cast<FreeBlock *>(previous);
    }
    FreeBlock *next = link_block(block->next_word);
    next->prev_word = with_link(next->prev_word, link_target(block->prev_word));
    FreeBlock *prev = link_block(block->prev_word);
    prev->next_word = with_link(prev->next_word, link_target(block->next_word));
}

// Takes a free block out of the bin of its length.
inline void unlink_free(Bins *bins, FreeBlock *block)
{
    unlink_from_bin(bins, block, bin_of(units_of(block)));
}

// Adds a page to the bins, taking the page manager's lock; `retry` says whether to try again.
RT_ORIGINAL(0x0233bbe0, add_page, void(Engine *engine, bool *retry));
// The same, for a caller that holds the lock.
RT_ORIGINAL(0x0233bd20, add_page_locked, void(Engine *engine, bool *retry));

// The small allocations' loop (the engine's plain and aligned allocations): `take` allocates
// `bytes` from the bins and describes the page; when it finds nothing, a page is added and the
// allocation tried again for as long as that says to retry. A block is counted in its page's headers
// and in the engine.
template <typename Take>
uintptr_t allocate_small_retrying(Engine *engine, uint64_t bytes, AllocationInfo *info, bool locked, Take take)
{
    bool retry = false;     // set once; the page functions store it on every call
    do {
        PageView view;
        if (uintptr_t block = take(&view)) {
            for (PageHeader *header : {view.header_below, view.header_above}) {
                if (header)
                    header->block_count += 1;
            }
            info->bytes = bytes;
            info->page = view.page;
            info->unknown_0x08 = 0;
            engine->allocation_count += 1;
            engine->used_bytes += bytes;
            return block;
        }
        if (locked)
            add_page_locked(engine, &retry);
        else
            add_page(engine, &retry);
    } while (retry);
    return 0;
}

}  // namespace engine::scaleform_heap
