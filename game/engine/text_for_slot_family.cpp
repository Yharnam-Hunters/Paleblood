// SPDX-License-Identifier: GPL-2.0-or-later
// The family formats_one_of_three_texts_for_slot_0x80 (symbols/families.csv, docs/FAMILIES.md):
// copies of one function that, when its object's byte at +0x68 is set, formats one of three wide
// texts into a wide string on the thread's allocator and hands it to the second argument's
// virtual slot 0x80. The text is 0x04dbec16 when the third argument is nonzero; otherwise the
// object's slot 0x20 decides (0x04dbdf48 when it returns true, else 0x04dbdec8). The texts are
// formats with no arguments (0x02b62d00 is given none). The slot is read after the formatting.
#include <cstddef>
#include <cstdint>

#include "container.h"
#include "runtime/original.h"
#include "temporary_heap.h"
#include "wide_string.h"

namespace {

struct TextSource;
struct TextSourceVtable {
    void *unknown_slots[4];
    bool (*query_0x20)(TextSource *);
};
static_assert(offsetof(TextSourceVtable, query_0x20) == 0x20);

struct TextSource {
    const TextSourceVtable *vtable;
    uint8_t unknown_0x08[0x60];
    uint8_t enabled;
};
static_assert(offsetof(TextSource, enabled) == 0x68);

struct TextTarget;
struct TextTargetVtable {
    void *unknown_slots[16];
    void (*take_text_0x80)(TextTarget *, const char16_t *text);
};
static_assert(offsetof(TextTargetVtable, take_text_0x80) == 0x80);

struct TextTarget {
    const TextTargetVtable *vtable;
};

// The thread's heap (thread-local): its allocator is an object at +0x28.
struct ThreadHeap {
    uint8_t unknown_0x00[0x28];
    engine::Allocator allocator;
};
static_assert(offsetof(ThreadHeap, allocator) == 0x28);

RT_GLOBAL(0x04dbec16, flagged_text, const char16_t);
RT_GLOBAL(0x04dbdf48, queried_text, const char16_t);
RT_GLOBAL(0x04dbdec8, default_text, const char16_t);
RT_ORIGINAL(0x02b62d00, wide_string_format, void(engine::WideString *, const char16_t *format, ...));

engine::Allocator *thread_allocator()
{
    if (!engine::temporary_heap::thread_heap()) engine::temporary_heap::create_for_thread();
    return &reinterpret_cast<ThreadHeap *>(engine::temporary_heap::thread_heap())->allocator;
}

const char16_t *chosen_text(TextSource *self, int32_t flagged)
{
    if (flagged) return flagged_text.address();
    return self->vtable->query_0x20(self) ? queried_text.address() : default_text.address();
}

}  // namespace

extern "C" void bb_engine_formats_one_of_three_texts_for_slot_0x80(TextSource *self, TextTarget *target,
                                                                   int32_t flagged)
{
    if (!self->enabled) return;
    engine::Allocator *allocator = thread_allocator();
    engine::check_container_heap(allocator);
    engine::WideString text;   // its first word is left as the stack holds it, as in the copies
    text.allocator = allocator;
    text.capacity = engine::wide_string_inline_capacity - 1;
    text.size = 0;
    text.inline_text[0] = 0;
    text.owns = 1;
    wide_string_format(&text, chosen_text(self, flagged));
    target->vtable->take_text_0x80(target, text.capacity >= engine::wide_string_inline_capacity ? text.heap
                                                                                                 : text.inline_text);
    engine::wide_string_release(&text);
}
