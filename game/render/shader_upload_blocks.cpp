// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the shader upload manager's blocks (shader_upload_manager.h). The manager's memory is tiled by
// range records in address order; free ranges are also on a free list, both closed by the
// manager's sentinel range. A range's state word is 0 when free, 2 when handed out and 4 once its
// owner released it; an odd word points at a 32-bit state instead.
// - render_acquire_shader_upload_block_02aa9d50: under the manager's lock, one 16-byte-aligned
//   range from the allocator below.
// - render_allocate_shader_upload_range_02aa9db0: first fit on the free list for the size rounded
//   up to 16 bytes. The range is cut in up to three: the alignment padding in front and the rest
//   behind become new free ranges (each only when the node pool gives a record; without one the
//   padding or rest stays inside the handed-out range). The count of blocks in use goes up and the
//   free bytes go down by the handed-out range's length.
// - render_reclaim_shader_upload_blocks_02aaa860: under the lock, swaps each pending list out in
//   turn (+0xa8, +0xb0, +0x98, +0xa0), frees the released blocks, merges them with free neighbours
//   on both sides (dropping the merged records) and puts the rest back on their list. Then marks
//   the command context's shader preparation event as needing a reclaim.
#include <cstdint>

#include "finish_record_shader.h"
#include "present_buffer_pool.h"
#include "shader_upload_manager.h"

namespace {

using render::Field;
using render::ShaderUploadRange;
using render::ShaderUploadManager;

constexpr int32_t wait_forever = -1;
constexpr uint64_t upload_alignment = 0x10;
constexpr uint64_t size_granule = 0x10;
constexpr uintptr_t pool_alignment = 8;
constexpr uint64_t range_free = 0;
constexpr uint64_t range_handed_out = 2;
constexpr uint32_t range_released = 4;
constexpr uint64_t state_is_pointer = 1;
constexpr uint32_t reclaimed_event_bits = 3;
constexpr uint32_t reclaimed_argument_bit = 0x08000000;

void lock(ShaderUploadManager *manager)
{
    manager->lock.vtable->lock(&manager->lock, wait_forever);
}

void unlock(ShaderUploadManager *manager)
{
    manager->lock.vtable->unlock(&manager->lock);
}

void *node_pool(ShaderUploadManager *manager)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(manager);
    return reinterpret_cast<void *>((address + pool_alignment - 1) & ~(pool_alignment - 1));
}

uintptr_t range_end(const ShaderUploadRange *range)
{
    return range->following.get()->start.get();
}

uintptr_t align_up(uintptr_t address, uint64_t alignment)
{
    return (address + (alignment - 1)) & (0 - alignment);
}

// Puts `added` on the free list just before `range`.
void link_free_before(ShaderUploadRange *range, ShaderUploadRange *added)
{
    ShaderUploadRange *before = range->free_previous.get();
    ShaderUploadRange *after = before->next.get();
    before->next.set(added);
    added->next.set(after);
    added->free_previous.set(before);
    after->free_previous.set(added);
}

void clear_links(ShaderUploadRange *range)
{
    range->state.set(range_free);
    range->next.set(nullptr);
    range->free_previous.set(nullptr);
}

uint32_t range_state(const ShaderUploadRange *range)
{
    const uint64_t word = range->state.get();
    if (word & state_is_pointer)
        return Field<uint32_t>::read(reinterpret_cast<const void *>(word & ~state_is_pointer));
    return static_cast<uint32_t>(word);
}

// Swaps the list head with null through the executable's compare/exchange; returns what it held.
ShaderUploadRange *take_pending(Field<ShaderUploadRange *> *list)
{
    for (;;) {
        const uintptr_t expected = reinterpret_cast<uintptr_t>(list->get());
        if (render::present_pool_calls::compare_exchange_present_pool_marker(list, 0, expected) == expected)
            return reinterpret_cast<ShaderUploadRange *>(expected);
    }
}

void put_back_pending(Field<ShaderUploadRange *> *list, ShaderUploadRange *first, ShaderUploadRange *last)
{
    for (;;) {
        ShaderUploadRange *head = list->get();
        last->next.set(head);
        const uintptr_t expected = reinterpret_cast<uintptr_t>(head);
        if (render::present_pool_calls::compare_exchange_present_pool_marker(
                list, reinterpret_cast<uintptr_t>(first), expected) == expected)
            return;
    }
}

// Frees one released range: merges the free ranges above and below into it and puts the result
// on the free list between its nearest free neighbours.
void free_range(ShaderUploadManager *manager, ShaderUploadRange *range)
{
    ShaderUploadRange *sentinel = &manager->ranges;
    void *pool = node_pool(manager);
    manager->blocks_in_use.set(manager->blocks_in_use.get() - 1);
    manager->free_bytes.set(manager->free_bytes.get() + (range_end(range) - range->start.get()));

    ShaderUploadRange *above = range->following.get();
    while (above->state.get() == range_free && above != sentinel) {
        ShaderUploadRange *merged = above;
        above = merged->following.get();
        render::free_shader_upload_node(pool, merged);
    }
    for (;;) {
        ShaderUploadRange *below = range->previous.get();
        if (below->state.get() != range_free || below == sentinel)
            break;
        ShaderUploadRange *merged = range;
        range = below;
        merged->next.set(nullptr);
        merged->free_previous.set(nullptr);
        merged->following.set(nullptr);
        merged->previous.set(nullptr);
        merged->start.set(0);
        merged->state.set(range_free);
        render::free_shader_upload_node(pool, merged);
    }
    if (above != range->following.get()) {
        range->following.set(above);
        above->previous.set(range);
    }

    ShaderUploadRange *free_after = range;
    do
        free_after = free_after->following.get();
    while (free_after->state.get() != range_free);
    ShaderUploadRange *free_before = range;
    do
        free_before = free_before->previous.get();
    while (free_before->state.get() != range_free);
    free_before->next.set(range);
    range->next.set(free_after);
    range->free_previous.set(free_before);
    free_after->free_previous.set(range);
    range->state.set(range_free);
}

}  // namespace

extern "C" ShaderUploadRange *bb_render_allocate_shader_upload_range_02aa9db0(ShaderUploadManager *manager,
                                                                                uint64_t bytes, uint64_t alignment)
{
    const uint64_t size = (bytes + size_granule - 1) & ~(size_granule - 1);
    ShaderUploadRange *sentinel = &manager->ranges;
    ShaderUploadRange *range = nullptr;
    uint64_t padding = 0;
    uint64_t room = 0;
    for (Field<ShaderUploadRange *> *link = &sentinel->next;;) {
        range = link->get();
        if (range == sentinel)
            return nullptr;
        const uintptr_t start = range->start.get();
        const uintptr_t end = range_end(range);
        const uintptr_t aligned = align_up(start, alignment);
        padding = 0;
        room = 0;
        if (end >= aligned) {
            padding = aligned - start;
            room = end - aligned;
        }
        link = &range->next;
        if (room >= size)
            break;
    }
    manager->blocks_in_use.set(manager->blocks_in_use.get() + 1);

    if (padding != 0 || room > size) {
        void *pool = node_pool(manager);
        ShaderUploadRange *rest = render::allocate_shader_upload_node(pool);
        if (rest == nullptr) {
            range->start.set(align_up(range->start.get(), alignment));
        } else {
            ShaderUploadRange *after = range->following.get();
            range->following.set(rest);
            rest->previous.set(range);
            rest->following.set(after);
            after->previous.set(rest);
            clear_links(rest);

            uintptr_t start = range->start.get();
            // The original's misalignment test, kept as written for any alignment.
            const uint64_t padding_to_next = alignment - (start & (alignment - 1));
            if ((padding_to_next & ~alignment) != 0) {
                ShaderUploadRange *front = render::allocate_shader_upload_node(pool);
                if (front != nullptr) {
                    ShaderUploadRange *before = range->previous.get();
                    before->following.set(front);
                    front->previous.set(before);
                    front->following.set(range);
                    range->previous.set(front);
                    clear_links(front);
                    front->start.set(range->start.get());
                    link_free_before(range, front);
                }
                start = align_up(range->start.get(), alignment);
                range->start.set(start);
            }
            rest->start.set(start + size);
            link_free_before(range, rest);
        }
    }

    ShaderUploadRange *before = range->free_previous.get();
    ShaderUploadRange *after = range->next.get();
    before->next.set(after);
    after->free_previous.set(before);
    manager->free_bytes.set(manager->free_bytes.get() + (range->start.get() - range_end(range)));
    range->next.set(nullptr);
    range->free_previous.set(nullptr);
    range->state.set(range_handed_out);
    return range;
}

extern "C" ShaderUploadRange *bb_render_acquire_shader_upload_block_02aa9d50(ShaderUploadManager *manager,
                                                                               uint64_t bytes)
{
    lock(manager);
    ShaderUploadRange *block = render::allocate_shader_upload_range(manager, bytes, upload_alignment);
    unlock(manager);
    return block;
}

extern "C" void bb_render_reclaim_shader_upload_blocks_02aaa860(ShaderUploadManager *manager,
                                                               render::ShaderCleanupContext **commands)
{
    // The lists in the order the original drains them.
    constexpr int list_order[ShaderUploadManager::pending_list_count] = {2, 3, 0, 1};
    lock(manager);
    for (const int index : list_order) {
        Field<ShaderUploadRange *> *list = &manager->pending[index];
        ShaderUploadRange *first = take_pending(list);
        if (first == nullptr)
            continue;
        // A stand-in record whose `next` heads the blocks that stay pending.
        ShaderUploadRange kept{};
        kept.next.set(first);
        ShaderUploadRange *last_kept = &kept;
        ShaderUploadRange *next = nullptr;
        for (ShaderUploadRange *block = first; block != nullptr; block = next) {
            next = block->next.get();
            if (range_state(block) != range_released) {
                last_kept = block;
                continue;
            }
            free_range(manager, block);
            last_kept->next.set(next);
        }
        ShaderUploadRange *still_pending = kept.next.get();
        if (still_pending != nullptr)
            put_back_pending(list, still_pending, last_kept);
    }
    render::ShaderPreparationState &preparation = (*commands)->preparation;
    preparation.event.set(preparation.event.get() | reclaimed_event_bits);
    preparation.event_argument.set(preparation.event_argument.get() | reclaimed_argument_bit);
    unlock(manager);
}
