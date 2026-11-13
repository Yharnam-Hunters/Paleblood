// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the regular heap's check of one block (Dantelion2 DLRegularHeap; blocks and lists in
// regular_heap.h).
//
// engine_regular_heap_check_block_02485e20 (0x02485e20) takes a heap and a payload address and
// returns the address of the first word around that block that is broken, or null when the block
// looks sound; the engine's heap diagnostics call it. A payload that is not 16-aligned or outside
// the heap's blocks is reported (the reports return). Then, in order: the block's next word must
// point at a 16-aligned block inside the heap and after it; unless that next block is the closing
// block, the next block's own next word must too. A block in use must be followed by a block that
// knows it (its prev_in_use flag). A free block must be named by the next block's prev word and be
// followed by a block in use that does not think its predecessor is in use; a header-only free
// block ends the check there. A free block's list links must each be its list's sentinel or a
// 16-aligned address inside the heap, and the blocks they name must link back to it.
#include <cstdint>

#include "engine.h"
#include "regular_heap.h"

namespace {

using namespace engine::regular_heap;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLRegularHeap.cpp"
RT_GLOBAL(0x04d3c6d4, source_file, const char);
// "p alignment faild." (sic)
RT_GLOBAL(0x04d3c7ee, message_misaligned, const char);
constexpr int32_t line_misaligned = 0x500;
// "given memory block does not seem to belong here."
RT_GLOBAL(0x04d3c801, message_foreign, const char);
constexpr int32_t line_foreign = 0x501;

constexpr uintptr_t payload_alignment_mask = 0xf;
constexpr uintptr_t block_alignment_bits = 0xc;  // bits 2 and 3: the flags take bits 0 and 1
// The sentinels a list link may name: those of lists 5 to 63 (no block is smaller than 32 bytes).
constexpr int first_used_list = 5;

template <typename T> uintptr_t word_address(T *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

// Whether a block's next word describes a block inside [first, end] after the block.
bool next_is_sound(const Block *block, const Block *first, const Block *end)
{
    const uintptr_t next = block->next_and_flags & ~block_flags;
    return (next & block_alignment_bits) == 0 && next >= address_of(first) && next <= address_of(end) &&
           next > address_of(block);
}

// Whether a free block's list link is sound: its list's sentinel, or (when it is no sentinel at
// all) a 16-aligned address in [first, end).
bool link_is_sound(Heap *heap, const Block *link, const Block *sentinel, const Block *first, const Block *end)
{
    const uintptr_t at = address_of(link);
    if (at >= address_of(list_sentinel(heap, first_used_list)) && at <= address_of(list_sentinel(heap, list_count - 1)))
        return link == sentinel;
    return (at & payload_alignment_mask) == 0 && at >= address_of(first) && at < address_of(end);
}

}  // namespace

extern "C" uintptr_t bb_engine_regular_heap_check_block_02485e20(Heap *heap, void *payload)
{
    Block *first = heap->first_block;
    Block *end = heap->end_block;
    const uintptr_t at = address_of(payload);
    if (at & payload_alignment_mask)
        engine::fatal_error(source_file.address(), line_misaligned, message_misaligned.address());
    if (at < address_of(first) || at > address_of(end))
        engine::fatal_error(source_file.address(), line_foreign, message_foreign.address());

    Block *block = reinterpret_cast<Block *>(at - block_header_size);
    if (!next_is_sound(block, first, end))
        return word_address(&block->next_and_flags);
    Block *next = reinterpret_cast<Block *>(block->next_and_flags & ~block_flags);
    const uintptr_t next_word = next->next_and_flags;
    if (next != heap->end_block && !next_is_sound(next, first, end))
        return word_address(&next->next_and_flags);

    if (block->next_and_flags & block_in_use)
        return (next_word & prev_in_use) ? 0 : word_address(&next->next_and_flags);
    if (next->prev != block)
        return word_address(&next->prev);
    if ((next_word & prev_in_use) || !(next_word & block_in_use))
        return word_address(&next->next_and_flags);
    if (at == address_of(next))
        return 0;

    Block *sentinel = list_sentinel(heap, list_index(address_of(next) - address_of(block)));
    Block *list_prev = block->list_prev;
    Block *list_next = block->list_next;
    if (!link_is_sound(heap, list_prev, sentinel, first, end))
        return word_address(&block->list_prev);
    if (!link_is_sound(heap, list_next, sentinel, first, end))
        return word_address(&block->list_next);
    if (list_prev->list_next != block)
        return word_address(&list_prev->list_next);
    if (list_next->list_prev != block)
        return word_address(&list_next->list_prev);
    return 0;
}
