// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the per-thread temporary heap's operations (Dantelion2 DLTemporaryHeap; layout in
// temporary_heap.h). Each works on the calling thread's heap, which all but the destructor and the
// size setting first create when the thread has none (temporary_heap_create.cpp).
//
// - engine_temporary_heap_destroy_for_thread_0247d3a0 (0x0247d3a0): a thread that has a heap has
//   its range cleared, its memory given back to the default allocator (the quit query singleton
//   and the default allocator are made first when missing, as on creation) and its slot cleared.
// - engine_temporary_heap_set_size_0247d430 (0x0247d430) sets the size the thread's heap will be
//   created with (0 for the default, 0x20000 bytes).
// - engine_temporary_heap_ensure_0247d590 (0x0247d590) and its identical copy
//   engine_temporary_heap_ensure_0247dd30 (0x0247dd30) only create the heap.
// - engine_temporary_heap_allocator_0247d470 (0x0247d470): the heap object's allocator interface
//   (its first vtable, 0x20 into the object).
// - engine_temporary_heap_mark_0247d5b0 (0x0247d5b0) and engine_temporary_heap_free_until_0247d5f0
//   (0x0247d5f0): the bottom as a mark, and the rewind to one.
// - The allocator interface's methods (their first argument, the interface, is not used):
//   engine_temporary_heap_allocate_0247d4b0 (0x0247d4b0) and
//   engine_temporary_heap_allocate_aligned_0247d510 (0x0247d510) from the bottom,
//   engine_temporary_heap_allocate_top_0247dd60 (0x0247dd60) and
//   engine_temporary_heap_allocate_aligned_top_0247dde0 (0x0247dde0) from the top, and
//   engine_temporary_heap_free_0247e1a0 (0x0247e1a0) of a top block. They are the linear heap's
//   steps (linear_heap.h) on the thread's heap.
#include <cstddef>
#include <cstdint>

#include "temporary_heap.h"

using namespace engine::temporary_heap;
namespace linear_heap = engine::linear_heap;

extern "C" void bb_engine_temporary_heap_destroy_for_thread_0247d3a0()
{
    TemporaryHeap *heap = thread_heap();
    if (!heap)
        return;
    heap->region = linear_heap::Heap{};
    BackingAllocator *allocator = prepare_default_allocator();
    allocator->vtable->free(allocator, heap);
    thread_heap() = nullptr;
}

extern "C" void bb_engine_temporary_heap_set_size_0247d430(uint64_t size)
{
    thread_heap_size() = size ? size : default_size;
}

extern "C" void bb_engine_temporary_heap_ensure_0247d590()
{
    if (!thread_heap())
        create_for_thread();
}

extern "C" void bb_engine_temporary_heap_ensure_0247dd30()
{
    if (!thread_heap())
        create_for_thread();
}

extern "C" uintptr_t bb_engine_temporary_heap_allocator_0247d470()
{
    // (Address arithmetic, so a heap whose creation failed gives 0x20 as in the original.)
    return reinterpret_cast<uintptr_t>(require_thread_heap()) + offsetof(TemporaryHeap, vtable);
}

extern "C" uintptr_t bb_engine_temporary_heap_mark_0247d5b0()
{
    return require_thread_heap()->bottom;
}

extern "C" void bb_engine_temporary_heap_free_until_0247d5f0(uintptr_t mark)
{
    linear_heap::free_until(require_thread_heap(), mark);
}

extern "C" void *bb_engine_temporary_heap_allocate_0247d4b0(void *, uint64_t size)
{
    return linear_heap::allocate(require_thread_heap(), size);
}

extern "C" void *bb_engine_temporary_heap_allocate_aligned_0247d510(void *, uint64_t size, uint64_t alignment)
{
    return linear_heap::allocate_aligned(require_thread_heap(), size, alignment);
}

extern "C" void *bb_engine_temporary_heap_allocate_top_0247dd60(void *, uint64_t size)
{
    return linear_heap::allocate_top(require_thread_heap(), size);
}

extern "C" void *bb_engine_temporary_heap_allocate_aligned_top_0247dde0(void *, uint64_t size, uint64_t alignment)
{
    return linear_heap::allocate_aligned_top(require_thread_heap(), size, alignment);
}

extern "C" void bb_engine_temporary_heap_free_0247e1a0(void *, void *memory)
{
    linear_heap::free_top(require_thread_heap(), memory);
}
