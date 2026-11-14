// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the regular heap's set-up (Dantelion2 DLRegularHeap; block layout in regular_heap.h).
//
// engine_regular_heap_initialize_02485500 (0x02485500) takes a heap and the memory it is to
// manage. The usable range starts where the first payload can be 16-aligned and ends at the
// last 16-aligned address that leaves the end block room. A range under 0x40 bytes is refused:
// the heap object is zeroed (the C library's memset) and the result is false. Otherwise every
// list is emptied and the range becomes one free block in its list, closed by the end block
// (in use, the free block before it), and the free bytes and capacity are that block's size.
#include <cstdint>

#include "regular_heap.h"
#include "runtime/original.h"

namespace {

using namespace engine::regular_heap;

RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t, uint64_t));

constexpr uint64_t alignment_mask = 0xf;
constexpr uint64_t smallest_range = 0x40;

}  // namespace

extern "C" bool bb_engine_regular_heap_initialize_02485500(Heap *heap, void *memory, uint64_t size)
{
    // The first block starts at least 8 bytes in, so that its payload can be 16-aligned.
    const uintptr_t first_payload = (address_of(memory) + block_header_size + alignment_mask - 8)
                                    & ~alignment_mask;
    const uintptr_t range_end = (address_of(memory) + size - 8) & ~alignment_mask;
    if (range_end - first_payload < smallest_range) {
        c_memset(heap, 0, sizeof(Heap));
        return false;
    }
    heap->lowest_payload = first_payload;
    heap->highest_payload = range_end;
    for (int index = 0; index < list_count; ++index) {
        Block *sentinel = list_sentinel(heap, index);
        heap->lists[index].next = sentinel;
        heap->lists[index].prev = sentinel;
    }

    Block *first = reinterpret_cast<Block *>(first_payload - block_header_size);
    Block *end = reinterpret_cast<Block *>(range_end - block_header_size);
    first->next_and_flags = address_of(end) | prev_in_use;
    end->prev = first;
    end->next_and_flags = address_of(first) | block_in_use;
    heap->first_block = first;
    heap->end_block = end;
    const uint64_t free_bytes = address_of(end) - address_of(first);
    heap->available = free_bytes;
    heap->capacity = free_bytes;
    heap->allocation_count = 0;
    heap->nonempty_lists = 0;
    push_free_block(heap, first, end);
    return true;
}
