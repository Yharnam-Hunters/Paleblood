// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: six small helpers that the FIOS objects and the load event queue call as originals
// (fios.h, resource_queue.h).
// - loading_allocator_allocate_headed_00829a40: allocates a block with a 16-byte header in front
//   of its data (the allocator's allocate, for the size plus the header in 32 bits), writes the
//   allocator and the data size into the header and returns the data. The allocation is not
//   checked: a null one is written through, as in the original.
// - loading_allocator_free_headed_00829a90: frees a block that carries a 16-byte header in front
//   of its data (a word cleared before the free, then the data size): the allocator's sized free
//   gets the header's address and the size plus the header. A null block is ignored.
// - loading_tagged_string_release_00836840: a string word below 2 is left alone; otherwise an
//   odd word (bit 0: owned) frees its block through the thread's allocator, and the word is
//   cleared either way.
// - loading_resource_handle_construct_024bd470 / _destroy_024bd4c0: the base resource handle,
//   which holds a reference to a resource info: constructing takes one (0x02a3a300), destroying
//   drops it (0x024bd930); a null info is skipped. Both store the base class vtable.
// - loading_transfer_task_cancel_024dd8a0: atomically sets the cancel bit (and the flag bit when
//   asked) in the task's state, unless its closed bit is already set; reports true either way.
#include <atomic>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/thread.h"
#include "fios.h"
#include "resource_queue.h"

namespace {

using namespace loading::fios;
using namespace loading::resource_queue;

struct BlockHeader {
    uint64_t word;       // the allocator it came from; cleared before the block is freed
    uint32_t data_size;  // the size of the data after the header
    uint32_t unknown_0x0c;
};
static_assert(sizeof(BlockHeader) == 0x10);

constexpr uintptr_t string_owned_bit = 1;
constexpr uintptr_t first_string_word = 2;   // words below this hold no string

ThreadAllocator *thread_allocator()
{
    auto *holder = *reinterpret_cast<ThreadAllocatorHolder **>(engine::thread_pointer() + thread_allocator_offset.get());
    return holder->allocator;
}

RT_GLOBAL(0x057b4b20, resource_handle_vtable, const uint8_t);

}  // namespace

extern "C" void bb_loading_allocator_free_headed_00829a90(ThreadAllocator *allocator, void *data)
{
    if (!data)
        return;
    auto *header = static_cast<BlockHeader *>(data) - 1;
    header->word = 0;
    // The size is 32-bit, so a header added to a size near the top wraps.
    const uint32_t size = header->data_size + static_cast<uint32_t>(sizeof(BlockHeader));
    allocator->vtable->free_sized(allocator, header, size);
}

extern "C" void bb_loading_tagged_string_release_00836840(uintptr_t *string)
{
    const uintptr_t word = *string;
    if (word < first_string_word)
        return;
    if (word & string_owned_bit)
        allocator_free_headed(thread_allocator(), reinterpret_cast<void *>(word - string_owned_bit));
    *string = 0;
}

extern "C" void bb_loading_resource_handle_construct_024bd470(ResourceHandle *handle, ResourceInfo *info)
{
    handle->vtable = resource_handle_vtable.address() + engine::vtable_address_point;
    handle->info = info;
    if (info)
        resource_info_add_reference(info);
}

extern "C" void bb_loading_resource_handle_destroy_024bd4c0(ResourceHandle *handle)
{
    handle->vtable = resource_handle_vtable.address() + engine::vtable_address_point;
    if (handle->info)
        resource_info_release(handle->info);
}

extern "C" uint8_t bb_loading_transfer_task_cancel_024dd8a0(TransferTask *task, bool with_flag)
{
    const uint32_t bits = transfer_task_cancel_bit | (with_flag ? transfer_task_flag_bit : 0);
    std::atomic_ref<uint32_t> state(task->state);
    uint32_t seen = state.load(std::memory_order_relaxed);
    while (!(seen & transfer_task_closed_bit)) {
        if (state.compare_exchange_weak(seen, seen | bits))
            break;
    }
    return true;
}

extern "C" void *bb_loading_allocator_allocate_headed_00829a40(ThreadAllocator *allocator, uint32_t size)
{
    // The size is 32-bit, so a header added to a size near the top wraps.
    const uint32_t total = size + static_cast<uint32_t>(sizeof(BlockHeader));
    auto *header = static_cast<BlockHeader *>(allocator->vtable->allocate(allocator, total));
    header->word = reinterpret_cast<uintptr_t>(allocator);
    header->data_size = size;
    return header + 1;
}
