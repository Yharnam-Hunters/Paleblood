// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the resource load event queue's array of events (structs in resource_queue.h).
//
// loading_load_event_array_push_back_02a69b70 (0x02a69b70) appends a copy of a load event; the
// load event queue (0x02a698e0, 0x02a69cd0) calls it. A full array first grows to one and a half
// times its count (engine/container.h). An event inside the array itself is found again in the
// new buffer. The copy takes the event's two words, copy-constructs the request and gives it the
// queued request's vtable. With no buffer after the growth (a failed allocation that was only
// reported), nothing is constructed, but the end still moves on by one event, as in the game.
//
// loading_load_event_array_reallocate_02a6bc50 (0x02a6bc50) moves the array into a buffer of
// exactly `capacity` events from its allocator: the events are copied as above, the old ones
// destroyed (the request's destructor: base vtable back, its data buffer freed, its three strings
// released and emptied, last declared first) and the old buffer freed. A failed allocation goes
// to the engine's transaction handling (engine/transaction.h) and the code goes on with the null
// buffer, as the game does.
//
// loading_stream_event_array_push_back_02a6d010 (0x02a6d010) and
// loading_stream_event_array_reallocate_02a6f140 (0x02a6f140) are the same code for the stream load
// event queue's array (0x02a6cd90 appends to it): its events carry one more byte, which is copied
// too, and a copied request gets the stream queue's vtable.
#include <cstdint>

#include "../engine/transaction.h"
#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

void construct_copy(LoadEvent *slot, const LoadEvent &source, const uint8_t *request_vtable)
{
    slot->word_0x00 = source.word_0x00;
    slot->word_0x08 = source.word_0x08;
    load_request_copy(&slot->request, &source.request);
    slot->request.vtable = request_vtable + engine::vtable_address_point;
}

void construct_copy(LoadEvent *slot, const LoadEvent &source)
{
    construct_copy(slot, source, queued_request_vtable.address());
}

void construct_copy(StreamLoadEvent *slot, const StreamLoadEvent &source)
{
    construct_copy(&slot->event, source.event, stream_request_vtable.address());
    slot->byte_0x158 = source.byte_0x158;
}

LoadRequest *request_of(LoadEvent *event) { return &event->request; }
LoadRequest *request_of(StreamLoadEvent *event) { return &event->event.request; }

template <typename Event> void reallocate(engine::Array<Event> *array, uint64_t capacity)
{
    auto *buffer = static_cast<Event *>(
        array->allocator->allocate(capacity * sizeof(Event), engine::array_buffer_alignment));
    if (!buffer)
        engine::report_failed_allocation();

    Event *old_begin = array->begin;
    Event *old_end = array->end;
    for (Event *from = old_begin, *to = buffer; from != old_end; ++from, ++to) {
        if (to)
            construct_copy(to, *from);
    }
    if (old_begin) {
        for (Event *event = old_begin; event != old_end; ++event)
            destroy_request(request_of(event));
        array->allocator->free(old_begin);
    }
    array->capacity_end = buffer + capacity;
    array->end = buffer + (old_end - old_begin);
    array->begin = buffer;
}

template <typename Event, typename Reallocate>
void push_back(engine::Array<Event> *array, const Event *event, Reallocate grow)
{
    const bool inside_array = array->contains(event);
    if (array->end == array->capacity_end) {
        const uint64_t index = inside_array ? event - array->begin : 0;
        grow(array, engine::array_grown_capacity<Event>(array->count()));
        if (inside_array)
            event = array->begin + index;
    }
    if (array->end)
        construct_copy(array->end, *event);
    array->end += 1;
}

}  // namespace

extern "C" void bb_loading_load_event_array_reallocate_02a6bc50(LoadEventArray *array, uint64_t capacity)
{
    reallocate(array, capacity);
}

extern "C" void bb_loading_load_event_array_push_back_02a69b70(LoadEventArray *array, const LoadEvent *event)
{
    push_back(array, event, load_event_array_reallocate);
}

extern "C" void bb_loading_stream_event_array_reallocate_02a6f140(StreamLoadEventArray *array, uint64_t capacity)
{
    reallocate(array, capacity);
}

extern "C" void bb_loading_stream_event_array_push_back_02a6d010(StreamLoadEventArray *array,
                                                                  const StreamLoadEvent *event)
{
    push_back(array, event, stream_event_array_reallocate);
}
