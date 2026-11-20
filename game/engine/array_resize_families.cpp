// SPDX-License-Identifier: GPL-2.0-or-later
// Five array families (symbols/families.csv, docs/FAMILIES.md):
// - reallocates_array_of_8_byte_4_aligned_elements: the copies of an engine array's reallocation
//   to a given capacity (engine/array_reserve.h) whose 8-byte elements ask for 4-byte alignment;
//   ten more copies of the same code are the reallocations engine/array64_push_back.cpp calls;
// - reallocates_array_of_16_byte_elements: the same for 16-byte elements (8-byte alignment);
// - reallocates_array_of_24_byte_handle_records: the same for records of a counted handle and a
//   dword, which are copy-constructed into the new buffer and destroyed in the old;
// - erases_range_of_24_byte_handle_records: the copies of the erase of a range of those records
//   (assigned down with 0x02fa7bc0, destroyed with 0x02fa7940);
// - resizes_heap_array_of_8_byte_elements: the copies of a resize of an array of 8-byte elements
//   (data, count, capacity) whose buffer comes from the heap the global at 0x057e7320 points at.
// Which arrays they serve is not traced here. The element types follow the code: the 8-byte
// elements move as one qword with 4-byte alignment, the 16-byte ones as two qwords with 8-byte
// alignment; the resized array's elements are never touched, only its buffer's size (8 per element).
#include <cstddef>
#include <cstdint>

#include "array_reserve.h"
#include "container.h"
#include "transaction.h"
#include "runtime/original.h"

namespace {

struct Element8 {
    uint32_t word_0x0;
    uint32_t word_0x4;
};
static_assert(sizeof(Element8) == 8 && alignof(Element8) == 4);
using Array8 = engine::Array<Element8>;

struct Element16 {
    uint64_t word_0x0;
    uint64_t word_0x8;
};
static_assert(sizeof(Element16) == 16 && alignof(Element16) == 8);
using Array16 = engine::Array<Element16>;

// A counted handle that 0x02fa7870 copy-constructs and 0x02fa7940 destroys (two qwords).
struct Handle {
    uint64_t unknown_0x0;
    uint64_t unknown_0x8;
};
// The 24-byte records: a handle and a dword, copied with the handle's own copy.
struct HandleRecord {
    Handle handle;
    uint32_t value_0x10;
};
static_assert(sizeof(HandleRecord) == 0x18 && alignof(HandleRecord) == 8);
static_assert(offsetof(HandleRecord, value_0x10) == 0x10);
using HandleRecordArray = engine::Array<HandleRecord>;

RT_ORIGINAL(0x02fa7870, handle_copy_construct, void(Handle *to, const Handle *from));
RT_ORIGINAL(0x02fa7940, handle_destroy, void(Handle *handle));
RT_ORIGINAL(0x02fa7bc0, handle_assign, void(Handle *to, const Handle *from));

// An iterator over the records as the range erase takes them (by value) and returns one: only
// the record pointer at +0x10 is read or written.
struct HandleRecordIterator {
    uint64_t unknown_0x0;
    uint64_t unknown_0x8;
    HandleRecord *record;
};
static_assert(sizeof(HandleRecordIterator) == 0x18 && offsetof(HandleRecordIterator, record) == 0x10);

// The resized array: its buffer, its element count and its capacity, in elements.
struct HeapArray {
    uint64_t *data;
    uint64_t count;
    uint64_t capacity;
};
static_assert(offsetof(HeapArray, count) == 0x8 && offsetof(HeapArray, capacity) == 0x10);

// What the heap's allocate slot is given by address: the members store only this dword.
struct AllocationTag {
    uint32_t value;
};
constexpr uint32_t array_allocation_tag = 2;

struct Heap;
struct HeapVtable {
    void *unknown_slots[12];
    void *(*reallocate)(Heap *, void *data, uint64_t size);
    void (*free)(Heap *, void *data);
    // The second argument is the resize's own second argument (the callers pass the array).
    void *(*allocate)(Heap *, const void *owner, uint64_t size, const AllocationTag *tag);
};
static_assert(offsetof(HeapVtable, reallocate) == 0x60 && offsetof(HeapVtable, free) == 0x68);
static_assert(offsetof(HeapVtable, allocate) == 0x70);

struct Heap {
    const HeapVtable *vtable;
};

RT_GLOBAL(0x057e7320, array_heap, Heap *);

// Capacities are kept in multiples of four elements.
constexpr uint64_t capacity_granule = 4;
// Growth past the capacity adds a quarter of the count.
constexpr uint64_t growth_divisor = 4;
// A smaller count shrinks the buffer only when it is under this fraction (a half) of the capacity.
constexpr uint64_t shrink_divisor = 2;

// The new capacity: none frees the buffer; any other is rounded up to the granule and the buffer
// reallocated (or allocated when there is none), the result stored unchecked.
void set_heap_array_capacity(HeapArray *array, const void *owner, uint64_t capacity)
{
    if (capacity == 0) {
        if (array->data) {
            Heap *heap = array_heap.get();
            heap->vtable->free(heap, array->data);
            array->data = nullptr;
        }
        array->capacity = 0;
        return;
    }
    capacity = (capacity + capacity_granule - 1) & ~(capacity_granule - 1);
    const uint64_t size = capacity * sizeof(uint64_t);
    Heap *heap = array_heap.get();
    if (array->data) {
        array->data = static_cast<uint64_t *>(heap->vtable->reallocate(heap, array->data, size));
    } else {
        const AllocationTag tag{array_allocation_tag};
        array->data = static_cast<uint64_t *>(heap->vtable->allocate(heap, owner, size, &tag));
    }
    array->capacity = capacity;
}

}  // namespace

extern "C" void bb_engine_reallocates_array_of_8_byte_4_aligned_elements(Array8 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_reallocates_array_of_16_byte_elements(Array16 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

// A smaller count shrinks the buffer to it only when that is under half the capacity; a larger
// one past the capacity grows it to a quarter more than the count. The count is then set.
extern "C" void bb_engine_resizes_heap_array_of_8_byte_elements(HeapArray *array, const void *owner, uint64_t count)
{
    const uint64_t capacity = array->capacity;
    if (count < array->count) {
        if (capacity / shrink_divisor > count && capacity != count)
            set_heap_array_capacity(array, owner, count);
    } else if (capacity < count) {
        const uint64_t grown = count + count / growth_divisor;
        if (capacity != grown)
            set_heap_array_capacity(array, owner, grown);
    }
    array->count = count;
}

// engine/array_reserve.h's reallocation with the records copy-constructed and the old ones
// destroyed: the copies run to the end read before them; the begin and end are read again after
// them (for the destruction and the count), and the begin and allocator again for the free.
extern "C" void bb_engine_reallocates_array_of_24_byte_handle_records(HandleRecordArray *array, uint64_t capacity)
{
    auto *buffer = static_cast<HandleRecord *>(
        array->allocator->allocate(capacity * sizeof(HandleRecord), alignof(HandleRecord)));
    if (!buffer) engine::report_failed_allocation();
    // Each placement checks its own address: with a null buffer the first is skipped.
    uintptr_t to = reinterpret_cast<uintptr_t>(buffer);
    const HandleRecord *const copy_end = array->end;
    for (const HandleRecord *from = array->begin; from != copy_end; ++from, to += sizeof(HandleRecord)) {
        if (!to) continue;
        auto *record = reinterpret_cast<HandleRecord *>(to);
        handle_copy_construct(&record->handle, &from->handle);
        record->value_0x10 = from->value_0x10;
    }
    HandleRecord *const begin = array->begin;
    HandleRecord *const end = array->end;
    if (begin) {
        for (HandleRecord *record = begin; record != end; ++record)
            handle_destroy(&record->handle);
        array->allocator->free(array->begin);
    }
    const uint64_t count = end - begin;
    const uintptr_t base = reinterpret_cast<uintptr_t>(buffer);
    array->capacity_end = reinterpret_cast<HandleRecord *>(base + capacity * sizeof(HandleRecord));
    array->end = reinterpret_cast<HandleRecord *>(base + count * sizeof(HandleRecord));
    array->begin = buffer;
}

// Erases the records in [first, last) and returns first in *result, of which only the record
// pointer is written. The whole array (first at the begin, last at the end) has every record
// destroyed and its end set to the begin read again after them; otherwise the records after the
// range are assigned down over it up to the end read before them, and the records from the new
// end up to the end read again after them (or to last, when nothing moved) are destroyed.
extern "C" HandleRecordIterator *bb_engine_erases_range_of_24_byte_handle_records(
    HandleRecordIterator *result, HandleRecordArray *array, HandleRecordIterator first, HandleRecordIterator last)
{
    HandleRecord *const begin = array->begin;
    if (first.record == begin && last.record == array->end) {
        HandleRecord *const end = last.record;
        HandleRecord *new_end = begin;
        if (begin != end) {
            for (HandleRecord *record = begin; record != end; ++record)
                handle_destroy(&record->handle);
            new_end = array->begin;
        }
        array->end = new_end;
        result->record = begin;
        return result;
    }
    if (first.record != last.record) {
        HandleRecord *const end = array->end;
        HandleRecord *kept_end = first.record;
        HandleRecord *destroy_end = last.record;
        if (last.record != end) {
            for (const HandleRecord *from = last.record; from != end; ++from, ++kept_end) {
                handle_assign(&kept_end->handle, &from->handle);
                kept_end->value_0x10 = from->value_0x10;
            }
            destroy_end = array->end;
        }
        for (HandleRecord *record = kept_end; record != destroy_end; ++record)
            handle_destroy(&record->handle);
        array->end = kept_end;
    }
    result->record = first.record;
    return result;
}
