// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the file load event queue's dispatch of its queued events and its start of a file
// transfer task (structs in resource_queue.h and transfer_task.h).
//
// loading_load_queue_dispatch_02a6a7f0 (0x02a6a7f0): under the queue's lock, hands every queued
// event to its handler by kind: 0 starts a file task (0x02a6a9e0), 3 and 4 go to 0x02a6abc0 and
// 0x02a6ad50 (each with the queue's transfer tasks; the kind 3 handler does not take the queue),
// 1 and 2 are skipped, and a larger kind is reported. Then all events are erased (0x02a6b6b0,
// the engine array's erase, given iterators by value).
//
// loading_load_queue_start_file_task_02a6a9e0 (0x02a6a9e0): the transfer tasks are asserted.
// Under the pool's lock a free file task slot is taken; with none, a new block of 32 slots is
// allocated from the pool's heap, cleared and constructed (0x02a6b520), chained in front of the
// pool's blocks, and its slots 1..31 become the free list (slot 0 is taken). With a slot, the file
// task is initialized for the event's info and request (the queue's manager and transfer task
// manager, flag 0); when that succeeds it is appended to the first transfer task list under the
// tasks' lock. Without one (the block allocation failed) the queue reports the request with code
// -0xfa.
#include <cstdint>

#include "../engine/engine.h"
#include "transfer_task.h"

using namespace loading::resource_queue;
using loading::transfer_task::file_task_initialize;
using loading::transfer_task::FileTransferTask;
using loading::transfer_task::HeapWordSource;
using loading::transfer_task::TransferTaskManager;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace loading::resource_queue {

// The pool of file tasks: blocks of 32 slots (each block's last word chains the blocks) on a heap,
// and a list of free slots chained through each task's +0x330.
struct FileTaskBlock {
    FileLoadSlots slots;
    FileTaskBlock *next;
};
static_assert(sizeof(FileTaskBlock) == 0x6708);
struct FileTaskPool {
    JobLock lock;
    uint8_t unknown_0x08[0x10];
    engine::Allocator *heap;
    FileTaskBlock *blocks;
    FileTransferTask *free_tasks;
};
static_assert(offsetof(FileTaskPool, heap) == 0x18 && offsetof(FileTaskPool, free_tasks) == 0x28);

}  // namespace loading::resource_queue

namespace {

RT_ORIGINAL(0x0247c4e0, heap_allocate, void *(uint64_t size, uint64_t alignment, engine::Allocator *));
RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t value, uint64_t bytes));
RT_ORIGINAL(0x02a6b520, file_load_slots_construct, void(FileLoadSlots *));
RT_ORIGINAL(0x02a6b340, transfer_task_list_push_back, void(engine::Array<TransferTask *> *, TransferTask *const *));
RT_ORIGINAL(0x02a6a9e0, load_queue_start_file_task, void(LoadEventQueue *, LoadEvent *, TransferTasks *));
RT_ORIGINAL(0x02a6abc0, load_queue_handle_kind_3, void(void *unused, LoadEvent *, TransferTasks *));
RT_ORIGINAL(0x02a6ad50, load_queue_handle_kind_4, void(LoadEventQueue *, LoadEvent *, TransferTasks *));

// The engine array's iterator as the erase takes it by value: two words the dispatch leaves
// unset, then the element.
struct EventIterator {
    uint64_t unknown_0x00;
    uint64_t unknown_0x08;
    LoadEvent *at;
};
RT_ORIGINAL(0x02a6b6b0, load_event_array_erase, EventIterator(LoadEventArray *, EventIterator first, EventIterator last));

constexpr uint64_t block_alignment = 8;
constexpr int32_t request_no_file_task = -0xfa;
constexpr uint32_t load_event_kind_3 = 3;
constexpr uint32_t last_load_event_kind = 4;
constexpr int32_t start_file_task_line = 0x1ec;
constexpr int32_t dispatch_line = 0x1d5;
RT_GLOBAL(0x04c6bdb1, invalid_event_kind_message, const char);

FileTransferTask *slot_task(FileTaskBlock *block, int index)
{
    return reinterpret_cast<FileTransferTask *>(&block->slots.slots[index]);
}

FileTransferTask *take_file_task(FileTaskPool *pool)
{
    pool->lock.vtable->lock(&pool->lock, wait_forever);
    FileTransferTask *task = pool->free_tasks;
    if (task) {
        pool->free_tasks = task->next_free;
    } else {
        // The original goes on with a null block when the allocation fails (and faults).
        auto *block = static_cast<FileTaskBlock *>(heap_allocate(sizeof(FileTaskBlock), block_alignment, pool->heap));
        if (block) {
            c_memset(block, 0, sizeof(FileTaskBlock));
            file_load_slots_construct(&block->slots);
        }
        block->next = pool->blocks;
        pool->blocks = block;
        for (int index = 1; index < file_load_slot_count - 1; ++index)
            slot_task(block, index)->next_free = slot_task(block, index + 1);
        slot_task(block, file_load_slot_count - 1)->next_free = nullptr;
        pool->free_tasks = slot_task(block, 1);
        task = slot_task(block, 0);
    }
    pool->lock.vtable->unlock(&pool->lock);
    return task;
}

}  // namespace

extern "C" void bb_loading_load_queue_start_file_task_02a6a9e0(LoadEventQueue *queue, LoadEvent *event,
                                                               TransferTasks *tasks)
{
    if (!tasks)
        engine::fatal_error(load_queue_source_file.address(), start_file_task_line, invalid_parameter_message.address());
    FileTransferTask *task = take_file_task(tasks->file_task_pool);
    if (!task) {
        queue->vtable->report_failure(queue, &event->request.word_0x80, request_no_file_task, event->request.word_0x20);
        return;
    }
    const bool initialized = file_task_initialize(task, queue, reinterpret_cast<const HeapWordSource *>(queue->manager),
                                                  static_cast<TransferTaskManager *>(queue->transfer_task_manager),
                                                  reinterpret_cast<ResourceInfo *>(event->word_0x08), &event->request, 0);
    if (!initialized)
        return;
    TransferTask *queued = reinterpret_cast<TransferTask *>(task);
    auto *lock = reinterpret_cast<JobLock *>(&tasks->lock);   // its exclusive lock, through its vtable
    lock->vtable->lock(lock, wait_forever);
    transfer_task_list_push_back(&tasks->lists[0], &queued);
    lock->vtable->unlock(lock);
}

extern "C" void bb_loading_load_queue_dispatch_02a6a7f0(LoadEventQueue *queue)
{
    queue->lock.vtable->lock(&queue->lock, wait_forever);
    const auto storage = reinterpret_cast<uintptr_t>(queue->event_storage);
    auto *events = reinterpret_cast<LoadEventArray *>((storage + event_storage_alignment - 1) &
                                                      ~(event_storage_alignment - 1));
    for (LoadEvent *event = events->begin, *end = events->end; event != end; ++event) {
        const auto kind = static_cast<uint32_t>(event->word_0x00);
        if (kind > last_load_event_kind)
            engine::fatal_error(load_queue_source_file.address(), dispatch_line, invalid_event_kind_message.address());
        else if (kind == static_cast<uint32_t>(load_event_kind_0))
            load_queue_start_file_task(queue, event, queue->transfer_tasks);
        else if (kind == load_event_kind_3)
            load_queue_handle_kind_3(nullptr, event, queue->transfer_tasks);
        else if (kind == static_cast<uint32_t>(load_event_kind_4))
            load_queue_handle_kind_4(queue, event, queue->transfer_tasks);
    }
    EventIterator first{}, last{};
    first.at = events->begin;
    last.at = events->end;
    load_event_array_erase(events, first, last);
    queue->lock.vtable->unlock(&queue->lock);
}
