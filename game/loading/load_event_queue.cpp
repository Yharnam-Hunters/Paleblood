// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: queuing a resource load request, and taking a resource out of the file load queue
// (structs in resource_queue.h).
//
// loading_load_queue_request_02a698e0 (0x02a698e0) and loading_load_queue_request_kind_4_02a69cd0
// (0x02a69cd0) are the same code with a different event kind (0 and 4); 0x01380850 calls both,
// each on its own queue. A request that is not valid (its vtable's first answer) gets an empty
// handle. Otherwise the queue's resource manager (read through a getter, here
// read directly) gives the info for the request (0x024c17e0); with
// none, the queue's first vtable slot reports the request (its words from +0x80, the code -0xf8,
// its word +0x20) and the handle is empty. With an info, the info remembers the queue (+0x68)
// and, under the queue's lock, an event of the kind with the info and a copy of the request (the
// queued request's vtable) is appended to the queue's events; the local copy is then destroyed.
// The returned handle holds a reference to the info, taken from a local handle that is dropped
// afterwards (so the reference count goes up by two and down by one).
//
// loading_load_queue_remove_02a6af30 (0x02a6af30), called by the resource manager's release
// (0x024c1530) and 0x024c6ee0, finds the transfer task loading the info in the file load queue's six
// task lists (in order, under their read-write lock) and cancels it, all under the queue's lock.
// A queue without its task lists is reported ("Invalid Parameter...", and the pointer read again).
//
// loading_load_queue_cancel_event_02a6abc0 (0x02a6abc0), the dispatch's handler of kind 3 events
// (0x02a6a7f0): the same search for the task loading the event's info, in the given task lists
// (asserted, not read again), and its cancel. It does not take the queue or its lock (the dispatch
// holds it).
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

constexpr int32_t cancel_event_line = 0x25e;

void lock(JobLock *lock) { lock->vtable->lock(lock, wait_forever); }
void unlock(JobLock *lock) { lock->vtable->unlock(lock); }

LoadEventArray *events_of(LoadEventQueue *queue)
{
    const auto storage = reinterpret_cast<uintptr_t>(queue->event_storage);
    const uintptr_t aligned = (storage + event_storage_alignment - 1) & ~(event_storage_alignment - 1);
    return reinterpret_cast<LoadEventArray *>(aligned);
}

void construct_handle(ResourceHandle *handle, ResourceInfo *info)
{
    resource_handle_construct(handle, info);
    handle->vtable = load_handle_vtable.address() + engine::vtable_address_point;
}

ResourceHandle *queue_request(ResourceHandle *result, LoadEventQueue *queue, LoadRequest *request, int32_t kind)
{
    if (!request_is_valid(request)) {
        construct_handle(result, nullptr);
        return result;
    }
    ResourceInfo *info = resource_manager_info_for(queue->manager, request);
    if (!info) {
        queue->vtable->report_failure(queue, &request->word_0x80, request_not_acquired, request->word_0x20);
        construct_handle(result, nullptr);
        return result;
    }

    info->load_queue = queue;
    ResourceHandle held;
    construct_handle(&held, info);
    lock(&queue->lock);
    LoadEvent event;
    event.word_0x00 = static_cast<uint32_t>(kind);
    event.word_0x08 = reinterpret_cast<uint64_t>(info);
    load_request_copy(&event.request, request);
    event.request.vtable = queued_request_vtable.address() + engine::vtable_address_point;
    load_event_array_push_back(events_of(queue), &event);
    destroy_request(&event.request);
    unlock(&queue->lock);
    construct_handle(result, held.info);
    resource_handle_destroy(&held);
    return result;
}

// The first task in the list loading `info`, or null.
TransferTask *find_task(const engine::Array<TransferTask *> &list, const ResourceInfo *info)
{
    for (TransferTask *const *task = list.begin; task != list.end; ++task) {
        if ((*task)->info == info)
            return *task;
    }
    return nullptr;
}

// The first task loading `info` in the six lists, in order, under their read-write lock; or null.
TransferTask *find_task_in_lists(TransferTasks *tasks, const ResourceInfo *info)
{
    read_write_lock_read(&tasks->lock, wait_forever);
    TransferTask *found = nullptr;
    for (const engine::Array<TransferTask *> &list : tasks->lists) {
        found = find_task(list, info);
        if (found)
            break;
    }
    read_write_lock_unlock(&tasks->lock);
    return found;
}

}  // namespace

extern "C" ResourceHandle *bb_loading_load_queue_request_02a698e0(ResourceHandle *result, LoadEventQueue *queue,
                                                                  LoadRequest *request)
{
    return queue_request(result, queue, request, load_event_kind_0);
}

extern "C" ResourceHandle *bb_loading_load_queue_request_kind_4_02a69cd0(ResourceHandle *result,
                                                                         LoadEventQueue *queue, LoadRequest *request)
{
    return queue_request(result, queue, request, load_event_kind_4);
}

extern "C" void bb_loading_load_queue_remove_02a6af30(LoadEventQueue *queue, ResourceInfo *info)
{
    lock(&queue->lock);
    TransferTasks *tasks = queue->transfer_tasks;
    if (!tasks) {
        engine::fatal_error(load_queue_source_file.address(), load_queue_remove_line,
                            invalid_parameter_message.address());
        tasks = queue->transfer_tasks;
    }
    TransferTask *found = find_task_in_lists(tasks, info);
    if (found)
        transfer_task_cancel(found, false);
    unlock(&queue->lock);
}

extern "C" void bb_loading_load_queue_cancel_event_02a6abc0(void *, LoadEvent *event, TransferTasks *tasks)
{
    if (!tasks)
        engine::fatal_error(load_queue_source_file.address(), cancel_event_line, invalid_parameter_message.address());
    TransferTask *found = find_task_in_lists(tasks, reinterpret_cast<ResourceInfo *>(event->word_0x08));
    if (found)
        transfer_task_cancel(found, false);
}
