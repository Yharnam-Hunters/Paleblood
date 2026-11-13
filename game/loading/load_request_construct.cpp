// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: constructing a resource load request (structs in resource_queue.h).
//
// loading_load_request_construct_024be250 (0x024be250) builds a load request (the base class:
// its vtable) from a name and one word, which it keeps at +0x20; the file load slot constructor
// (0x024d63f0), 0x024db570 and 0x013e7290 call it. The words around that word are cleared and the
// two bytes at +0x40 start as 0xff and 0. The name string takes its heap from the request heap
// (checked as containers check a heap: a heap without the valid flag is reported and used
// anyway), and is assigned the name's text: its wcslen, or 0 characters for an empty name
// without asking wcslen. The second and third strings and the data array are built by
// 0x024be8e0; the words at +0x80 and the fields at +0x138 and +0x140 are cleared.
#include <cstdint>

#include "../engine/container.h"
#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

constexpr uint8_t constructed_byte_0x40 = 0xff;

}  // namespace

extern "C" void bb_loading_load_request_construct_024be250(LoadRequest *request, const char16_t *name,
                                                           uint64_t word_0x20)
{
    request->vtable = base_request_vtable.address() + engine::vtable_address_point;
    request->word_0x18 = 0;
    request->word_0x10 = 0;
    request->word_0x08 = 0;
    request->word_0x20 = word_0x20;
    request->word_0x38 = 0;
    request->word_0x30 = 0;
    request->word_0x28 = 0;
    request->byte_0x40 = constructed_byte_0x40;
    request->byte_0x41 = 0;

    auto *heap = reinterpret_cast<engine::Allocator *>(request_heap_owner() + request_heap_offset);
    engine::check_container_heap(heap);
    request->name.allocator = heap;
    request->name.capacity = engine::wide_string_inline_capacity - 1;
    request->name.size = 0;
    request->name.inline_text[0] = u'\0';
    const uint64_t length = name[0] != u'\0' ? c_wcslen(name) : 0;
    wide_string_assign_counted(&request->name, name, length);
    request->name.owns = 1;

    request->word_0x98 = 0;
    request->word_0x90 = 0;
    request->word_0x88 = 0;
    request->word_0x80 = 0;
    load_request_construct_tail(&request->text_0xa0);
    request->word_0x140 = 0;
    request->value_0x138 = 0;
}
