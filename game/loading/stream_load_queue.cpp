// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: queuing a request in the stream load queue, the stream handle's constructor, and the
// front of a queue of finished entries (structs in resource_queue.h).
//
// loading_stream_queue_request_02a6cd90 (0x02a6cd90): the stream queue's version of the load
// queue's request (0x02a698e0). A request that is not valid gets an empty stream handle; with no
// info from the manager the queue's first vtable slot reports it (its words from +0x80, the code
// -0xf8, its word +0x20) and the handle is empty. Otherwise a local stream handle for the info and
// the queue is made (which makes the info remember the queue) and, under the queue's lock, an
// event (kind 0, the info, a copy of the request with the stream request's vtable, and the
// request's extra byte) is appended to the queue's events; the local copy is then destroyed. The
// returned handle copies the local one (a new reference, the stream handle's vtable, the queue)
// before the local one is dropped.
//
// loading_stream_handle_construct_02a70a90 (0x02a70a90): a resource handle for the info, with the
// stream handle's vtable and the queue; when both are set the info remembers the queue (the
// original tail-calls a one-line setter for it).
//
// loading_finished_queue_front_02a69fb0 (0x02a69fb0): under the lock, copies the first entry of
// a non-empty list into `out` and returns true; false (nothing written) when the count is 0.
// loading_finished_queue_pop_02a6a020 (0x02a6a020): under the lock, unlinks the first node (when
// the list is not empty by its links), destroys its entry, frees the node through the list's
// allocator and counts one less.
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

void lock(JobLock *lock) { lock->vtable->lock(lock, wait_forever); }
void unlock(JobLock *lock) { lock->vtable->unlock(lock); }

template <typename T> T *aligned_in(uint8_t *storage, uintptr_t alignment)
{
    const auto address = reinterpret_cast<uintptr_t>(storage);
    return reinterpret_cast<T *>((address + alignment - 1) & ~(alignment - 1));
}

}  // namespace

extern "C" void bb_loading_stream_handle_construct_02a70a90(StreamHandle *handle, ResourceInfo *info,
                                                            StreamLoadQueue *queue)
{
    resource_handle_construct(&handle->handle, info);
    handle->handle.vtable = stream_handle_vtable.address() + engine::vtable_address_point;
    handle->queue = queue;
    if (info && queue)
        info->stream_queue = queue;
}

extern "C" StreamHandle *bb_loading_stream_queue_request_02a6cd90(StreamHandle *result, StreamLoadQueue *queue,
                                                                  StreamLoadRequest *stream_request)
{
    LoadRequest *request = &stream_request->request;
    if (!request_is_valid(request)) {
        stream_handle_construct(result, nullptr, nullptr);
        return result;
    }
    ResourceInfo *info = resource_manager_info_for(queue->manager, request);
    if (!info) {
        // the same first vtable slot as the load queue's
        queue->vtable->report_failure(reinterpret_cast<LoadEventQueue *>(queue), &request->word_0x80,
                                      request_not_acquired, request->word_0x20);
        stream_handle_construct(result, nullptr, nullptr);
        return result;
    }

    StreamHandle held;
    stream_handle_construct(&held, info, queue);
    lock(&queue->lock);
    StreamLoadEvent event;
    event.event.word_0x00 = static_cast<uint32_t>(stream_event_kind);
    event.event.word_0x08 = reinterpret_cast<uint64_t>(info);
    load_request_copy(&event.event.request, request);
    event.event.request.vtable = stream_request_vtable.address() + engine::vtable_address_point;
    event.byte_0x158 = stream_request->byte_0x148;
    stream_event_array_push_back(aligned_in<StreamLoadEventArray>(queue->event_storage, stream_event_storage_alignment),
                                 &event);
    destroy_request(&event.event.request);
    unlock(&queue->lock);

    resource_handle_construct(&result->handle, held.handle.info);
    result->handle.vtable = stream_handle_vtable.address() + engine::vtable_address_point;
    result->queue = held.queue;
    resource_handle_destroy(&held.handle);
    return result;
}

extern "C" bool bb_loading_finished_queue_front_02a69fb0(FinishedQueue *queue, FinishedEntry *out)
{
    lock(&queue->lock);
    FinishedList *list = aligned_in<FinishedList>(queue->finished_storage, finished_storage_alignment);
    bool found = false;
    if (list->count) {
        finished_entry_copy(out, &list->head->next->entry);
        found = true;
    }
    unlock(&queue->lock);
    return found;
}

extern "C" void bb_loading_finished_queue_pop_02a6a020(FinishedQueue *queue)
{
    lock(&queue->lock);
    FinishedList *list = aligned_in<FinishedList>(queue->finished_storage, finished_storage_alignment);
    FinishedNode *first = list->head->next;
    if (first != list->head) {
        first->previous->next = first->next;
        first->next->previous = first->previous;
        first->entry.vtable->destroy(&first->entry);
        list->allocator->free(first);
        list->count -= 1;
    }
    unlock(&queue->lock);
}
