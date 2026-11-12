// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: copying a resource load request, and building the last part of one (structs in
// resource_queue.h).
//
// loading_load_request_copy_024be3b0 (0x024be3b0) is the request's copy constructor: the load
// event queues call it when they copy events (0x02a69b70, 0x02a6bc50 and five others), and
// 0x013e7290. It gives the copy the base class's vtable (the queue puts its own on afterwards),
// copies the words +0x08..+0x38, the bytes +0x40/+0x41, the words +0x80..+0x98, +0x138 and +0x140,
// and copies the three strings and the data array. The name string is built on the request heap
// (checked as containers check a heap) and assigned the source's first `size` characters; the
// other two strings and the array are first built empty on the default heap by 0x024be8e0, then
// assigned. A string's owns byte is copied from the source. The bytes +0x42..+0x47 and
// +0x13c..+0x13f are left as they were. The second and third strings are assigned only when the
// request is not copied onto itself (the name string and the array are assigned regardless).
//
// loading_load_request_construct_tail_024be8e0 (0x024be8e0), called by both constructors, builds
// the second and third strings empty (owning) and the data array empty, each on the default
// container heap, which it looks up (and makes on first use) again for each, with the heap check.
#include <cstdint>

#include "../engine/container.h"
#include "../engine/engine.h"
#include "../engine/static_wide_string.h"
#include "../engine/wide_string_assign.h"
#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

// The default container heap, made on first use.
engine::Allocator *default_container_heap()
{
    engine::Allocator *&heap = engine::static_wide_string::default_allocator.get();
    if (!heap)
        heap = engine::static_wide_string::acquire_default_allocator();
    return heap;
}

void begin_owned_string(WideString *s)
{
    engine::Allocator *heap = default_container_heap();
    engine::check_container_heap(heap);
    engine::wide_string_begin(s, heap);
    s->owns = 1;
}

void copy_string(WideString *target, const WideString &source, bool same_request)
{
    target->owns = source.owns;
    if (!same_request)
        engine::wide_string_copy_assign(target, source);
}

}  // namespace

extern "C" void bb_loading_load_request_construct_tail_024be8e0(LoadRequestTail *tail)
{
    begin_owned_string(&tail->text_0xa0);
    begin_owned_string(&tail->text_0xd8);
    engine::Allocator *heap = default_container_heap();
    engine::check_container_heap(heap);
    tail->data.begin = nullptr;
    tail->data.end = nullptr;
    tail->data.capacity_end = nullptr;
    tail->data.allocator = heap;
}

extern "C" void bb_loading_load_request_copy_024be3b0(LoadRequest *copy, const LoadRequest *source)
{
    copy->vtable = base_request_vtable.address() + engine::vtable_address_point;
    copy->word_0x08 = source->word_0x08;
    copy->word_0x10 = source->word_0x10;
    copy->word_0x18 = source->word_0x18;
    copy->word_0x20 = source->word_0x20;
    copy->word_0x28 = source->word_0x28;
    copy->word_0x30 = source->word_0x30;
    copy->word_0x38 = source->word_0x38;
    copy->byte_0x40 = source->byte_0x40;
    copy->byte_0x41 = source->byte_0x41;

    auto *heap = reinterpret_cast<engine::Allocator *>(request_heap_owner() + request_heap_offset);
    const uint64_t name_size = source->name.size;
    engine::check_container_heap(heap);
    engine::wide_string_begin(&copy->name, heap);
    engine::wide_string_assign_from(&copy->name, &source->name, 0, name_size);
    copy->name.owns = source->name.owns;
    copy->word_0x98 = source->word_0x98;
    copy->word_0x90 = source->word_0x90;
    copy->word_0x88 = source->word_0x88;
    copy->word_0x80 = source->word_0x80;

    load_request_construct_tail(&copy->text_0xa0);
    copy->word_0x140 = source->word_0x140;
    const bool same_request = copy == source;
    copy_string(&copy->text_0xa0, source->text_0xa0, same_request);
    copy_string(&copy->text_0xd8, source->text_0xd8, same_request);
    byte_array_assign(&copy->data, &source->data);
    copy->value_0x138 = source->value_0x138;
}
