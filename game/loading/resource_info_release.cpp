// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: dropping a reference to a resource info (structs in resource_queue.h).
//
// loading_resource_info_release_024bd930 (0x024bd930) drops one reference to a resource info
// (Dantelion2 DLResourceInfo); the resource handles' releases (0x024bd310, 0x024bd360, ...) call
// it. While references remain, or once its release has started, that is all. The last reference
// starts the release, once: the flag is tested again and set under the engine's shared lock (the
// one jobs use too), so only one caller goes on. That caller hands the info to the resource
// manager's release and then takes the requests still pending on it away: while the pending list
// has entries and its first entry holds a request, it marks the info's pending requests cancelled,
// unlinks and frees that list node, and removes the request from the info's stream load queue. A
// first entry without a request stops the loop and stays in the list.
#include <cstdint>

#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

// The engine's lock shared by jobs and resource infos (declared with the jobs).
constexpr auto &shared_lock = frame_timing::job::job_lock;

ResourceManager *resource_manager()
{
    const uintptr_t storage = reinterpret_cast<uintptr_t>(resource_manager_storage.address());
    const uintptr_t aligned = (storage + resource_manager_alignment - 1) & ~(resource_manager_alignment - 1);
    return reinterpret_cast<ResourceManager *>(aligned);
}

// Sets the releasing flag under the shared lock; false when another caller set it first.
bool claim_release(ResourceInfo *info)
{
    frame_timing::job::JobLock &lock = shared_lock.get();
    lock.vtable->lock(&lock, frame_timing::job::wait_forever);
    const bool claimed = (info->flags & resource_releasing) == 0;
    if (claimed)
        info->flags |= resource_releasing;
    lock.vtable->unlock(&lock);
    return claimed;
}

void unlink_first_pending(ResourceInfo *info)
{
    PendingNode *head = info->pending_head;
    PendingNode *first = head->next;
    if (first == head)
        return;
    first->previous->next = first->next;
    first->next->previous = first->previous;
    info->pending_allocator->free(first);
    info->pending_count -= 1;
}

}  // namespace

extern "C" void bb_loading_resource_info_release_024bd930(ResourceInfo *info)
{
    resource_info_drop_reference(info);
    if (resource_info_reference_count(info) != 0 || (info->flags & resource_releasing))
        return;
    if (!claim_release(info))
        return;

    resource_manager_release(resource_manager(), info);
    while (info->pending_count != 0) {
        void *request = info->pending_head->next->request;
        if (!request)
            return;
        info->flags |= resource_pending_cancelled;
        unlink_first_pending(info);
        stream_queue_remove(info->stream_queue, request);
    }
}
