// SPDX-License-Identifier: GPL-2.0-or-later
// Dantelion2 DLNew's release of a heap buffer, which the engine inlines wherever an object frees
// memory it was handed: the heap a thread has set aside for its next release (taken, and cleared)
// or else the heap that owns the buffer. A buffer no heap owns is a fatal error.
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "engine.h"
#include "runtime/original.h"
#include "thread.h"

namespace engine {

// DLNew's per-thread state, at this offset from the thread pointer.
struct NewThreadState {
    uint8_t unknown_0x00[0x10];
    Allocator *release_heap;   // the heap the next release uses, once; null for the owner's heap
};
static_assert(offsetof(NewThreadState, release_heap) == 0x10);
RT_GLOBAL(0x057e44a8, new_thread_state_offset, const int64_t);

// The heap that owns a buffer (null when none does).
RT_ORIGINAL(0x0247b720, heap_owning, Allocator *(const void *buffer));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLNew.cpp"
RT_GLOBAL(0x04d3bac3, new_source_file, const char);
// "Heap not found for releasing buffer."
RT_GLOBAL(0x04d40207, heap_not_found_message, const char);
constexpr int32_t heap_not_found_line = 100;

inline NewThreadState *new_thread_state()
{
    return reinterpret_cast<NewThreadState *>(thread_pointer() + new_thread_state_offset.get());
}

// Frees a buffer. After the fatal error (which returns in the shipped game) the original goes on
// with a null heap and faults on it; so does this.
inline void release_buffer(void *buffer)
{
    NewThreadState *state = new_thread_state();
    Allocator *heap = state->release_heap;
    state->release_heap = nullptr;
    if (!heap) {
        heap = heap_owning(buffer);
        if (!heap) fatal_error(new_source_file.address(), heap_not_found_line, heap_not_found_message.address());
    }
    heap->free(buffer);
}

}  // namespace engine
