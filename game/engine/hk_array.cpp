// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: Havok's array growth (hkArrayUtil's reserve and reserve more), which the physics
// constructors (instance_construct_helpers.cpp, controller_body_set.cpp) and the instance update
// (instance_update.cpp) call as originals.
//
// Both ask the allocator for the new size in bytes through a pointer, and the allocator may round
// it up; the new capacity is that size divided by the element size (signed), stored over the
// capacity and its flags, so the array owns its new buffer. An array that owns its buffer has it
// reallocated (the allocator's slot 0x30, with the old capacity in bytes); one that does not gets
// a new buffer and its elements copied, and its old buffer is left alone.
// - engine_hk_array_reserve_00819040: grows to `count` elements. A failed allocation leaves the
//   array with no buffer and no capacity and returns 1 (Havok's failure); otherwise 0.
// - engine_hk_array_reserve_more_00819100: doubles the capacity from the size (1 element when the
//   array is empty). Nothing is checked: a failed allocation is stored as it is.
// The sizes are 32-bit products and wrap as the game's do.
#include <cstddef>
#include <cstdint>

#include "havok.h"
#include "runtime/original.h"

namespace {

using engine::HkArray;
using engine::HkMemoryAllocator;

// The allocator's buffer reallocation, the slot after the two havok.h names: the old buffer and
// its size in bytes, and the requested size, which it may round up and write back.
struct ReallocatingVtable {
    engine::HkMemoryAllocatorVtable named;
    void *(*buf_realloc)(HkMemoryAllocator *, void *data, int32_t old_bytes, int32_t *bytes);
};
static_assert(offsetof(ReallocatingVtable, buf_realloc) == 0x30);

// The game's memcpy.
RT_ORIGINAL(0x02fbe578, copy_bytes, void *(void *destination, const void *source, uint64_t bytes));

constexpr int32_t hk_success = 0;
constexpr int32_t hk_failure = 1;
constexpr int32_t empty_array_reserve = 1;   // elements reserve more asks for an empty array

int32_t wrapping_multiply(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

void *reallocate(HkMemoryAllocator *allocator, HkArray<void> *array, int32_t element_size, int32_t *bytes)
{
    const auto *vtable = reinterpret_cast<const ReallocatingVtable *>(allocator->vtable);
    const int32_t old_bytes = wrapping_multiply(engine::hk_capacity(array->capacity_and_flags), element_size);
    return vtable->buf_realloc(allocator, array->data, old_bytes, bytes);
}

void copy_elements(void *buffer, const HkArray<void> *array, int32_t element_size)
{
    const int64_t length = wrapping_multiply(array->size, element_size);
    copy_bytes(buffer, array->data, static_cast<uint64_t>(length));
}

void set_capacity(HkArray<void> *array, int32_t bytes, int32_t element_size)
{
    array->capacity_and_flags = static_cast<uint32_t>(bytes / element_size);
}

}  // namespace

extern "C" int32_t bb_engine_hk_array_reserve_00819040(HkMemoryAllocator *allocator, HkArray<void> *array,
                                                       int32_t count, int32_t element_size)
{
    int32_t bytes = wrapping_multiply(count, element_size);
    if (engine::hk_owns_buffer(array->capacity_and_flags)) {
        array->data = reallocate(allocator, array, element_size, &bytes);
        if (array->data == nullptr) {
            array->capacity_and_flags = 0;
            return hk_failure;
        }
    } else {
        void *buffer = allocator->vtable->buf_alloc(allocator, &bytes);
        if (buffer == nullptr) {
            array->data = nullptr;
            array->capacity_and_flags = 0;
            return hk_failure;
        }
        copy_elements(buffer, array, element_size);
        array->data = buffer;
    }
    set_capacity(array, bytes, element_size);
    return hk_success;
}

extern "C" void bb_engine_hk_array_reserve_more_00819100(HkMemoryAllocator *allocator, HkArray<void> *array,
                                                        int32_t element_size)
{
    const int32_t size = array->size;
    const int32_t count = size != 0 ? wrapping_multiply(size, 2) : empty_array_reserve;
    int32_t bytes = wrapping_multiply(count, element_size);
    if (engine::hk_owns_buffer(array->capacity_and_flags)) {
        array->data = reallocate(allocator, array, element_size, &bytes);
    } else {
        void *buffer = allocator->vtable->buf_alloc(allocator, &bytes);
        copy_elements(buffer, array, element_size);
        array->data = buffer;
    }
    set_capacity(array, bytes, element_size);
}
