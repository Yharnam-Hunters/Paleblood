// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's per-thread temporary heap (Dantelion2 DLTemporaryHeap): a linear heap (linear_heap.h)
// at the start of its own memory, which a thread-local slot points at. The engine's per-frame code
// takes scratch memory from it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "fixed_allocator.h"
#include "linear_heap.h"
#include "runtime/original.h"
#include "thread.h"

namespace engine::temporary_heap {

using fixed_allocator::BackingAllocator;

// The heap object: the linear heap's range and ends, then the vtables of its two bases, the
// second of which points back at the first.
struct TemporaryHeap {
    linear_heap::Heap region;
    const void *vtable;
    const void *allocator_vtable;
    const void **owner;
};
static_assert(sizeof(TemporaryHeap) == 0x38 && offsetof(TemporaryHeap, vtable) == 0x20);

// Where the thread's heap pointer and its size setting sit, from the thread pointer.
RT_GLOBAL(0x057e4568, thread_heap_offset, const int64_t);
RT_GLOBAL(0x057e4600, thread_heap_size_offset, const int64_t);

RT_GLOBAL(0x05aa54f8, quit_query_instance, void *);
RT_ORIGINAL(0x024e2550, quit_query_create, void *());
RT_GLOBAL(0x05940670, default_allocator, BackingAllocator *);
RT_ORIGINAL(0x02fa2b10, default_allocator_create, BackingAllocator *());

// The thread's heap creation (temporary_heap_create.cpp).
RT_ORIGINAL(0x0247d250, create_for_thread, void());

constexpr uint64_t default_size = 0x20000;

inline TemporaryHeap *&thread_heap()
{
    return *reinterpret_cast<TemporaryHeap **>(thread_pointer() + thread_heap_offset.get());
}

inline uint64_t &thread_heap_size()
{
    return *reinterpret_cast<uint64_t *>(thread_pointer() + thread_heap_size_offset.get());
}

// The quit query singleton and the default allocator, each made when missing (in that order).
inline BackingAllocator *prepare_default_allocator()
{
    if (!quit_query_instance.get())
        quit_query_create();
    BackingAllocator *allocator = default_allocator.get();
    if (!allocator) {
        allocator = default_allocator_create();
        default_allocator.get() = allocator;
    }
    return allocator;
}

// The thread's heap, created first when the thread has none (then read again, so it may still be
// null when the creation failed).
inline linear_heap::Heap *require_thread_heap()
{
    if (!thread_heap())
        create_for_thread();
    return reinterpret_cast<linear_heap::Heap *>(thread_heap());   // the region is its first member
}

}  // namespace engine::temporary_heap
