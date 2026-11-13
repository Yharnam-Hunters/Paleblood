// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the robust heap's check of one block (Dantelion2 DLRobustHeap; blocks, lists and tree
// in robust_heap.h).
//
// engine_robust_heap_check_block_02486080 (0x02486080) takes a heap and a payload address and
// returns the address of the first word around that block that is broken, or null when the block
// looks sound; the engine's heap diagnostics call it. A payload that is not 16-aligned or outside
// the heap's payload range is reported (the reports return). Then, in order: the block's next word
// must have clear alignment bits and point inside the range, after the block; unless the next
// block sits at the range's top, its own next word must too. A block in use must be followed by a
// block that knows it (its prev_in_use flag). A free block must be named by the next block's prev
// word and be followed by a block in use that does not think its predecessor is in use. A free
// block too small for links ends the check there. A small free block's list links must each be its
// list's sentinel or (when no sentinel at all) a 16-aligned node of a block inside the range, and
// the nodes they name must link back to it. A large free block must record its own size, and its
// tree links (left, right, parent without the colour bit) must each be null or inside the range.
#include <cstdint>

#include "engine.h"
#include "robust_heap.h"

namespace {

using namespace engine::robust_heap;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLRobustHeap.cpp"
RT_GLOBAL(0x04d3c78b, source_file, const char);
// "p alignment faild." (sic)
RT_GLOBAL(0x04d3c7ee, message_misaligned, const char);
constexpr int32_t line_misaligned = 0x1de;
// "given memory block does not seem to belong here."
RT_GLOBAL(0x04d3c801, message_foreign, const char);
constexpr int32_t line_foreign = 0x1df;

constexpr uintptr_t payload_alignment_mask = 0xf;
constexpr uintptr_t block_alignment_bits = 0xc;  // bits 2 and 3: the flags take bits 0 and 1
constexpr uintptr_t flag_bits = block_in_use | prev_in_use;

template <typename T> uintptr_t word_address(T *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

struct Range {
    uintptr_t lowest;
    uintptr_t highest;

    bool holds(uintptr_t at) const { return at >= lowest && at <= highest; }
};

// Whether a block's next word has clear alignment bits and names a block in the range after it.
bool next_is_sound(const Block *block, Range range)
{
    const uintptr_t word = block->next_and_flags;
    const uintptr_t next = word & ~flag_bits;
    return (word & block_alignment_bits) == 0 && range.holds(next) && next > word_address(block);
}

// Whether a small free block's list link is sound: its list's sentinel, or (when it is none of the
// sentinels) the 16-aligned node of a block in the range.
bool link_is_sound(Heap *heap, const FreeNode *link, const FreeNode *sentinel, Range range)
{
    const uintptr_t at = word_address(link);
    const uintptr_t bins = word_address(heap->small_bins);
    if (at >= bins && at < bins + small_bin_count * small_bin_stride)
        return link == sentinel;
    return (at & payload_alignment_mask) == 0 && range.holds(at - block_header_size);
}

bool null_or_in(uintptr_t at, Range range)
{
    return at == 0 || range.holds(at);
}

}  // namespace

extern "C" uintptr_t bb_engine_robust_heap_check_block_02486080(Heap *heap, void *payload)
{
    const Range range{heap->lowest_payload, heap->highest_payload};
    const uintptr_t at = word_address(payload);
    if (at & payload_alignment_mask)
        engine::fatal_error(source_file.address(), line_misaligned, message_misaligned.address());
    if (at < range.lowest || at > range.highest)
        engine::fatal_error(source_file.address(), line_foreign, message_foreign.address());

    Block *block = reinterpret_cast<Block *>(at - block_header_size);
    if (!next_is_sound(block, range))
        return word_address(&block->next_and_flags);
    Block *next = reinterpret_cast<Block *>(block->next_and_flags & ~flag_bits);
    const uintptr_t next_word = next->next_and_flags;
    // (The game compares the next block with the top of the payload range, not with a block.)
    if (word_address(next) != range.highest && !next_is_sound(next, range))
        return word_address(&next->next_and_flags);

    if (block->next_and_flags & block_in_use)
        return (next_word & prev_in_use) ? 0 : word_address(&next->next_and_flags);
    if (next->prev != block)
        return word_address(&next->prev);
    if ((next_word & prev_in_use) || !(next_word & block_in_use))
        return word_address(&next->next_and_flags);

    const uint64_t usable = word_address(next) - word_address(block) - block_overhead;
    if (usable < smallest_usable)
        return 0;
    FreeNode *node = &block->payload;
    if (usable <= largest_small) {
        FreeNode *sentinel = small_bin(heap, usable);
        FreeNode *list_next = node->next_or_left;
        if (!link_is_sound(heap, list_next, sentinel, range))
            return word_address(&node->next_or_left);
        FreeNode *list_prev = node->prev_or_right;
        if (!link_is_sound(heap, list_prev, sentinel, range))
            return word_address(&node->prev_or_right);
        if (list_next->prev_or_right != node)
            return word_address(&list_next->prev_or_right);
        if (list_prev->next_or_left != node)
            return word_address(&list_prev->next_or_left);
        return 0;
    }

    if (usable != node->size)
        return word_address(&node->size);
    if (!null_or_in(word_address(node->next_or_left), range))
        return word_address(&node->next_or_left);
    if (!null_or_in(word_address(node->prev_or_right), range))
        return word_address(&node->prev_or_right);
    if (!null_or_in(node->parent_and_colour & ~tree_colour, range))
        return word_address(&node->parent_and_colour);
    return 0;
}
