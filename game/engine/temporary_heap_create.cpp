// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the per-thread temporary heap's creation (Dantelion2 DLTemporaryHeap).
//
// engine_temporary_heap_create_for_thread_0247d250 (0x0247d250) makes sure the calling thread has
// its temporary heap: a bump region that the engine's per-frame code allocates scratch memory from
// (it is called on entry by well over a hundred functions). A thread that has one keeps it. The
// size comes from the thread's setting, 0x20000 bytes when that is 0; a setting under 0x38 bytes
// (the heap's own header) is reported. The quit query singleton (frame_step.cpp) is created first
// when missing, and the default allocator when missing; the heap's memory comes from the default
// allocator and the thread's slot is set to it. A failed allocation is reported, and the slot is
// read again: a null slot leaves the thread without a heap. The heap object sits at the start of
// its memory; its region runs from the end of the object to the end of the memory, empty.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"
#include "thread.h"

namespace {

using engine::fixed_allocator::BackingAllocator;

// The heap object: the region it hands out (twice: a start and end pair, and the current pair),
// then the vtables of its two bases, the second of which points back at the first.
struct TemporaryHeap {
    uint8_t *start;
    uint8_t *end;
    uint8_t *current_start;
    uint8_t *current_end;
    const void *vtable;
    const void *allocator_vtable;
    const void **owner;
};
static_assert(sizeof(TemporaryHeap) == 0x38 && offsetof(TemporaryHeap, vtable) == 0x20);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLTemporaryHeap.cpp"
RT_GLOBAL(0x04d3bb5e, source_file, const char);
// "Temporary heap setting is too small."
RT_GLOBAL(0x04d3bbc4, message_setting_too_small, const char);
constexpr int32_t line_setting_too_small = 0x101;
// "Could not allocate temporary heap for thread. Out of memory."
RT_GLOBAL(0x04d3bbe9, message_out_of_memory, const char);
constexpr int32_t line_out_of_memory = 0x104;

// Where the thread's heap pointer and its size setting sit, from the thread pointer.
RT_GLOBAL(0x057e4568, thread_heap_offset, const int64_t);
RT_GLOBAL(0x057e4600, thread_heap_size_offset, const int64_t);
RT_GLOBAL(0x05aa54f8, quit_query_instance, void *);
RT_ORIGINAL(0x024e2550, quit_query_create, void *());
RT_GLOBAL(0x05940670, default_allocator, BackingAllocator *);
RT_ORIGINAL(0x02fa2b10, default_allocator_create, BackingAllocator *());
RT_GLOBAL(0x0575b540, temporary_heap_vtable, const uint8_t);
// The second base's vtable: its address points 0x10 into this object (past the offset-to-top
// and type-info words).
RT_GLOBAL(0x057b3140, temporary_heap_allocator_vtable, const uint8_t);
constexpr std::ptrdiff_t vtable_entries_offset = 0x10;

constexpr uint64_t default_size = 0x20000;

TemporaryHeap *&thread_heap()
{
    return *reinterpret_cast<TemporaryHeap **>(engine::thread_pointer() + thread_heap_offset.get());
}

}  // namespace

extern "C" void bb_engine_temporary_heap_create_for_thread_0247d250()
{
    if (thread_heap())
        return;
    const uint64_t setting =
        *reinterpret_cast<const uint64_t *>(engine::thread_pointer() + thread_heap_size_offset.get());
    uint64_t size = default_size;
    if (setting) {
        if (setting < sizeof(TemporaryHeap))
            engine::fatal_error(source_file.address(), line_setting_too_small, message_setting_too_small.address());
        size = setting;
    }
    if (!quit_query_instance.get())
        quit_query_create();
    BackingAllocator *allocator = default_allocator.get();
    if (!allocator) {
        allocator = default_allocator_create();
        default_allocator.get() = allocator;
    }
    thread_heap() = static_cast<TemporaryHeap *>(allocator->vtable->allocate(allocator, size));
    if (!thread_heap())
        engine::fatal_error(source_file.address(), line_out_of_memory, message_out_of_memory.address());
    TemporaryHeap *heap = thread_heap();
    if (!heap)
        return;
    uint8_t *memory = reinterpret_cast<uint8_t *>(heap);
    *heap = TemporaryHeap{};
    heap->vtable = temporary_heap_vtable.address();
    heap->allocator_vtable = temporary_heap_allocator_vtable.address() + vtable_entries_offset;
    heap->owner = &heap->vtable;
    heap->current_start = memory + sizeof(TemporaryHeap);
    heap->start = memory + sizeof(TemporaryHeap);
    heap->current_end = memory + size;
    heap->end = memory + size;
}
