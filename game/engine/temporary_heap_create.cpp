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
#include "temporary_heap.h"

using namespace engine::temporary_heap;

namespace {

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLTemporaryHeap.cpp"
RT_GLOBAL(0x04d3bb5e, source_file, const char);
// "Temporary heap setting is too small."
RT_GLOBAL(0x04d3bbc4, message_setting_too_small, const char);
constexpr int32_t line_setting_too_small = 0x101;
// "Could not allocate temporary heap for thread. Out of memory."
RT_GLOBAL(0x04d3bbe9, message_out_of_memory, const char);
constexpr int32_t line_out_of_memory = 0x104;

RT_GLOBAL(0x0575b540, temporary_heap_vtable, const uint8_t);
// The second base's vtable: its address points 0x10 into this object (past the offset-to-top
// and type-info words).
RT_GLOBAL(0x057b3140, temporary_heap_allocator_vtable, const uint8_t);
constexpr std::ptrdiff_t vtable_entries_offset = 0x10;

}  // namespace

extern "C" void bb_engine_temporary_heap_create_for_thread_0247d250()
{
    if (thread_heap())
        return;
    const uint64_t setting = thread_heap_size();
    uint64_t size = default_size;
    if (setting) {
        if (setting < sizeof(TemporaryHeap))
            engine::fatal_error(source_file.address(), line_setting_too_small, message_setting_too_small.address());
        size = setting;
    }
    BackingAllocator *allocator = prepare_default_allocator();
    thread_heap() = static_cast<TemporaryHeap *>(allocator->vtable->allocate(allocator, size));
    if (!thread_heap())
        engine::fatal_error(source_file.address(), line_out_of_memory, message_out_of_memory.address());
    TemporaryHeap *heap = thread_heap();
    if (!heap)
        return;
    const uintptr_t memory = reinterpret_cast<uintptr_t>(heap);
    *heap = TemporaryHeap{};
    heap->vtable = temporary_heap_vtable.address();
    heap->allocator_vtable = temporary_heap_allocator_vtable.address() + vtable_entries_offset;
    heap->owner = &heap->vtable;
    heap->region.bottom = memory + sizeof(TemporaryHeap);
    heap->region.lowest = memory + sizeof(TemporaryHeap);
    heap->region.top = memory + size;
    heap->region.highest = memory + size;
}
