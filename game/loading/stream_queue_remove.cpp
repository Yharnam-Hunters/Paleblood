// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the stream load queue's removal of a request (structs in resource_queue.h).
//
// loading_stream_queue_remove_02a6d8e0 (0x02a6d8e0), called by the resource info's release
// (0x024bd930), 0x024bdb80, 0x02a70c60 and 0x02a70ca0. A request outside the queue's pool (before
// it, or past its end; the end itself counts as inside) is reported, and the removal goes on. The
// first entry for the request in the in-use array is erased (the rest moved down with memmove);
// then the request is appended to the array of requests to give back, whether or not it was in
// use.
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

RequestPointerArray *aligned_array(uint8_t *storage)
{
    const auto address = reinterpret_cast<uintptr_t>(storage);
    return reinterpret_cast<RequestPointerArray *>((address + request_storage_alignment - 1) &
                                                   ~(request_storage_alignment - 1));
}

}  // namespace

extern "C" void bb_loading_stream_queue_remove_02a6d8e0(StreamLoadQueue *queue, void *request)
{
    auto *at = static_cast<uint8_t *>(request);
    if (at < queue->pool || at > queue->pool + queue->pool_size)
        engine::fatal_error(stream_queue_source_file.address(), stream_queue_remove_line, out_of_range_message.address());

    RequestPointerArray *in_use = aligned_array(queue->in_use_storage);
    for (void **entry = in_use->begin; entry != in_use->end; ++entry) {
        if (*entry == request) {
            c_memmove(entry, entry + 1, reinterpret_cast<uint8_t *>(in_use->end) - reinterpret_cast<uint8_t *>(entry + 1));
            in_use->end -= 1;
            break;
        }
    }
    void *const released = request;
    request_pointer_array_push_back(aligned_array(queue->released_storage), &released);
}
