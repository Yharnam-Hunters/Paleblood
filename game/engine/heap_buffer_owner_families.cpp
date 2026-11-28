// SPDX-License-Identifier: GPL-2.0-or-later
// Four engine families (symbols/families.csv, docs/FAMILIES.md):
// - destroys_heap_buffer_owner_directly: the copies of one destructor body. It stores the owner's
//   vtable (0x055b5be0's address point), frees the buffer at +0x20 to the heap the global at
//   0x057e7320 points at (its slot 0x68), then lets go of the handle at +0x8;
// - destroys_heap_buffer_owner_through_base_at_0x38 and ..._through_base_at_0x48: the same body
//   entered with a pointer to a base 0x38 or 0x48 bytes into the owner (the offsets the copies
//   subtract);
// - frees_to_global_heap: the copies of a forward that frees its argument to that heap.
// The handle at +0x8 is null, a tagged value (low bit set) that is stepped down by one, or the
// address of a counted object whose count (the low 22 bits of the dword at +0x20) is dropped
// before 0x027ee1e0 is told; a count already at zero is left alone. The handle is read after the
// free. Which objects these are is not traced here.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace {

// The heap the global at 0x057e7320 points at; the members only use its free slot (0x68).
struct GlobalHeap;
struct GlobalHeapVtable {
    void *unknown_slots[13];
    void (*free)(GlobalHeap *, void *data);
};
static_assert(offsetof(GlobalHeapVtable, free) == 0x68);

struct GlobalHeap {
    const GlobalHeapVtable *vtable;
};

RT_GLOBAL(0x057e7320, global_heap, GlobalHeap *);

void free_to_global_heap(void *data)
{
    GlobalHeap *heap = global_heap.get();
    heap->vtable->free(heap, data);
}

// What a handle with its low bit clear points at: only the dword at +0x20 is read, whose low 22
// bits count references.
struct CountedObject {
    uint8_t unknown_0x0[0x20];
    uint32_t state;
};
static_assert(offsetof(CountedObject, state) == 0x20);
constexpr uint32_t counted_object_count_mask = 0x3fffff;
constexpr uint64_t handle_tag = 1;

RT_ORIGINAL(0x027ee1e0, counted_object_released, void(CountedObject *object));

void release_handle(uint64_t *handle)
{
    const uint64_t value = *handle;
    if (value == 0)
        return;
    if (value & handle_tag) {
        *handle = value - 1;
        return;
    }
    auto *object = reinterpret_cast<CountedObject *>(value);
    if ((object->state & counted_object_count_mask) == 0)
        return;
    object->state -= 1;
    counted_object_released(object);
}

// The owner as the destructor sees it: its vtable, the handle and the buffer.
struct HeapBufferOwner {
    const uint8_t *vtable;
    uint64_t handle;
    uint8_t unknown_0x10[0x10];
    void *buffer;
};
static_assert(offsetof(HeapBufferOwner, handle) == 0x8 && offsetof(HeapBufferOwner, buffer) == 0x20);

RT_GLOBAL(0x055b5be0, heap_buffer_owner_vtable, const uint8_t);

// The base the second and third families are entered through lies this far into the owner.
constexpr std::ptrdiff_t first_base_offset = 0x38;
constexpr std::ptrdiff_t second_base_offset = 0x48;

template <std::ptrdiff_t base_offset> void destroy_heap_buffer_owner(void *self)
{
    auto *owner = reinterpret_cast<HeapBufferOwner *>(static_cast<uint8_t *>(self) - base_offset);
    owner->vtable = heap_buffer_owner_vtable.address() + engine::vtable_address_point;
    free_to_global_heap(owner->buffer);
    release_handle(&owner->handle);
}

}  // namespace

extern "C" void bb_engine_destroys_heap_buffer_owner_directly(void *owner)
{
    destroy_heap_buffer_owner<0>(owner);
}

extern "C" void bb_engine_destroys_heap_buffer_owner_through_base_at_0x38(void *base)
{
    destroy_heap_buffer_owner<first_base_offset>(base);
}

extern "C" void bb_engine_destroys_heap_buffer_owner_through_base_at_0x48(void *base)
{
    destroy_heap_buffer_owner<second_base_offset>(base);
}

extern "C" void bb_engine_frees_to_global_heap(void *data)
{
    free_to_global_heap(data);
}
