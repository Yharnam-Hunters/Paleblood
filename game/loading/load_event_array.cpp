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
#include <cstdint>

#include "../engine/transaction.h"
#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

void construct_copy(LoadEvent *slot, const LoadEvent &source)
{
    slot->word_0x00 = source.word_0x00;
    slot->word_0x08 = source.word_0x08;
    load_request_copy(&slot->request, &source.request);
    slot->request.vtable = queued_request_vtable.address() + engine::vtable_address_point;
}

void empty(WideString *s)
{
    engine::wide_string_release(s);
    s->capacity = engine::wide_string_inline_capacity - 1;
    s->size = 0;
    s->inline_text[0] = u'\0';
}

// The request's destructor, inlined into the array's.
void destroy(LoadRequest *request)
{
    request->vtable = base_request_vtable.address() + engine::vtable_address_point;
    engine::Array<uint8_t> &data = request->data;
    if (data.begin) {
        data.allocator->free(data.begin);
        data.capacity_end = nullptr;
        data.end = nullptr;
        data.begin = nullptr;
    }
    empty(&request->text_0xd8);
    empty(&request->text_0xa0);
    empty(&request->name);
}

}  // namespace

extern "C" void bb_loading_load_event_array_reallocate_02a6bc50(LoadEventArray *array, uint64_t capacity)
{
    auto *buffer = static_cast<LoadEvent *>(
        array->allocator->allocate(capacity * sizeof(LoadEvent), engine::array_buffer_alignment));
    if (!buffer)
        engine::report_failed_allocation();

    LoadEvent *old_begin = array->begin;
    LoadEvent *old_end = array->end;
    for (LoadEvent *from = old_begin, *to = buffer; from != old_end; ++from, ++to) {
        if (to)
            construct_copy(to, *from);
    }
    if (old_begin) {
        for (LoadEvent *event = old_begin; event != old_end; ++event)
            destroy(&event->request);
        array->allocator->free(old_begin);
    }
    array->capacity_end = buffer + capacity;
    array->end = buffer + (old_end - old_begin);
    array->begin = buffer;
}

extern "C" void bb_loading_load_event_array_push_back_02a69b70(LoadEventArray *array, const LoadEvent *event)
{
    const bool inside_array = array->contains(event);
    if (array->end == array->capacity_end) {
        const uint64_t index = inside_array ? event - array->begin : 0;
        load_event_array_reallocate(array, engine::array_grown_capacity<LoadEvent>(array->count()));
        if (inside_array)
            event = array->begin + index;
    }
    if (array->end)
        construct_copy(array->end, *event);
    array->end += 1;
}
