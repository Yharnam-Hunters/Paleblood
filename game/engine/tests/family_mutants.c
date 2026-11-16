/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Engine-specific deliberately wrong replacements for the engine function families
   (game/engine/family_cases.py MUTANTS), built into libbbfamily_mutants beside the generic ones
   (test/family_mutants.c), whose bbgame_init records the image these reach through rt_guest. */
#include <stdint.h>

#include "runtime/guest.h"

/* The guard of the static wide string the initialize_static_wide_string members build. */
#define STATIC_WIDE_STRING_GUARD 0x05acebc8u

/* Publishes the guard and does nothing else: wrong for initialize_static_wide_string, which
   builds the string and registers its destructor first on a clear guard and leaves a set guard
   alone. */
void bb_mutant_static_wide_string_guard_only(void)
{
    *(volatile uint64_t *)rt_guest(STATIC_WIDE_STRING_GUARD) = 1;
}

/* The engine array (game/engine/container.h's Array) as array_resize_families.cpp's reallocations
   see it: begin, end and capacity end after a first qword, then its allocator, whose vtable's
   allocate (slot 0x58) and free (slot 0x70) they call. */
struct mutant_resize_array {
    uint64_t unknown_0x00;
    uint8_t *begin;
    uint8_t *end;
    uint8_t *capacity_end;
    void *allocator;
};
struct mutant_resize_allocator_vtable {
    void *unknown_slots[11];
    void *(*allocate)(void *allocator, uint64_t size, uint64_t alignment);
    void *unknown_slots_1[2];
    void (*free)(void *allocator, void *data);
};

/* A reallocation without the failure report: the elements copied forwards (a null buffer skips
   the copy), the old buffer freed unless keep_old, the three pointers set. */
static void mutant_resize_reserve(struct mutant_resize_array *array, uint64_t capacity, uint64_t element,
                                  uint64_t alignment, int keep_old)
{
    const struct mutant_resize_allocator_vtable *vtable =
        *(const struct mutant_resize_allocator_vtable **)array->allocator;
    uint8_t *buffer = vtable->allocate(array->allocator, capacity * element, alignment);
    uint64_t count = (uint64_t)(array->end - array->begin) / element;
    if (buffer)
        for (uint64_t i = 0; i < count * element; i++)
            buffer[i] = array->begin[i];
    if (array->begin && !keep_old)
        vtable->free(array->allocator, array->begin);
    array->capacity_end = buffer + capacity * element;
    array->end = buffer + count * element;
    array->begin = buffer;
}

#define MUTANT_RESIZE_ELEMENT_8 8u
#define MUTANT_RESIZE_ELEMENT_16 16u
#define MUTANT_RESIZE_ALIGN_4 4u
#define MUTANT_RESIZE_ALIGN_8 8u

/* Reallocates 8-byte elements asking for 8-byte alignment: wrong for
   reallocates_array_of_8_byte_4_aligned_elements, which asks for 4. */
void bb_mutant_array64_reserve_8_aligned(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_8, 0);
}

/* Reallocates 4-aligned 8-byte elements but keeps the old buffer: wrong for
   reallocates_array_of_8_byte_4_aligned_elements, which frees it. */
void bb_mutant_array64_4_aligned_reserve_keeps_old_buffer(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_4, 1);
}

/* Reallocates as if the elements were 8 bytes: wrong for reallocates_array_of_16_byte_elements
   (the size asked for, the copy and the pointers). */
void bb_mutant_array128_reserve_as_8_byte_elements(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_8, 0);
}

/* Reallocates 16-byte elements but keeps the old buffer: wrong for
   reallocates_array_of_16_byte_elements, which frees it. */
void bb_mutant_array128_reserve_keeps_old_buffer(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_16, MUTANT_RESIZE_ALIGN_8, 1);
}

/* The array resizes_heap_array_of_8_byte_elements resizes (data, count, capacity) and the heap
   the global at 0x057e7320 points at, whose vtable reallocates (slot 0x60), frees (0x68) and
   allocates (0x70, with an owner and a pointer to a dword). */
#define MUTANT_ARRAY_HEAP 0x057e7320u
#define MUTANT_HEAP_ALLOCATION_TAG 2u
struct mutant_heap_array {
    uint64_t *data;
    uint64_t count;
    uint64_t capacity;
};
struct mutant_heap_vtable {
    void *unknown_slots[12];
    void *(*reallocate)(void *heap, void *data, uint64_t size);
    void (*free)(void *heap, void *data);
    void *(*allocate)(void *heap, const void *owner, uint64_t size, const uint32_t *tag);
};

/* The capacity change: zero frees, any other is rounded up to a multiple of granule. */
static void mutant_heap_array_capacity(struct mutant_heap_array *array, const void *owner, uint64_t capacity,
                                       uint64_t granule)
{
    void *heap = *(void **)rt_guest(MUTANT_ARRAY_HEAP);
    const struct mutant_heap_vtable *vtable = *(const struct mutant_heap_vtable **)heap;
    if (capacity == 0) {
        if (array->data) {
            vtable->free(heap, array->data);
            array->data = 0;
        }
        array->capacity = 0;
        return;
    }
    capacity = (capacity + granule - 1) / granule * granule;
    if (array->data) {
        array->data = vtable->reallocate(heap, array->data, capacity * sizeof(uint64_t));
    } else {
        const uint32_t tag = MUTANT_HEAP_ALLOCATION_TAG;
        array->data = vtable->allocate(heap, owner, capacity * sizeof(uint64_t), &tag);
    }
    array->capacity = capacity;
}

#define MUTANT_HEAP_GRANULE 4u
#define MUTANT_HEAP_WRONG_GRANULE 8u
#define MUTANT_HEAP_GROWTH_DIVISOR 4u
#define MUTANT_HEAP_SHRINK_DIVISOR 2u

/* The resize with the growth, the shrink, the granule and the owner as given. */
static void mutant_heap_array_resize(struct mutant_heap_array *array, const void *owner, uint64_t count,
                                     int grow_by_quarter, int shrink, uint64_t granule)
{
    const uint64_t capacity = array->capacity;
    if (count < array->count) {
        if (shrink && capacity / MUTANT_HEAP_SHRINK_DIVISOR > count)
            mutant_heap_array_capacity(array, owner, count, granule);
    } else if (capacity < count) {
        const uint64_t grown = grow_by_quarter ? count + count / MUTANT_HEAP_GROWTH_DIVISOR : count;
        if (capacity != grown)
            mutant_heap_array_capacity(array, owner, grown, granule);
    }
    array->count = count;
}

/* Grows to the count itself: wrong for resizes_heap_array_of_8_byte_elements, which grows to a
   quarter more. */
void bb_mutant_heap_array_grows_to_count(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 0, 1, MUTANT_HEAP_GRANULE);
}

/* Never shrinks the buffer: wrong for resizes_heap_array_of_8_byte_elements, which shrinks it
   below half the capacity. */
void bb_mutant_heap_array_never_shrinks(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 1, 0, MUTANT_HEAP_GRANULE);
}

/* Rounds capacities up to eight elements: wrong for resizes_heap_array_of_8_byte_elements, which
   rounds to four. */
void bb_mutant_heap_array_rounds_to_8(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 1, 1, MUTANT_HEAP_WRONG_GRANULE);
}

/* Allocates for the array instead of the owner it is given: wrong for
   resizes_heap_array_of_8_byte_elements, which passes its second argument on. */
void bb_mutant_heap_array_allocates_for_array(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    (void)owner;
    mutant_heap_array_resize(array, array, count, 1, 1, MUTANT_HEAP_GRANULE);
}

/* The handle records reallocates_array_of_24_byte_handle_records moves: 24 bytes, a handle that
   0x02fa7870 copy-constructs and 0x02fa7940 destroys, then a dword at +0x10. */
#define MUTANT_HANDLE_COPY 0x02fa7870u
#define MUTANT_HANDLE_DESTROY 0x02fa7940u
#define MUTANT_HANDLE_RECORD 24u
#define MUTANT_HANDLE_RECORD_VALUE 0x10u
#define MUTANT_HANDLE_RECORD_ALIGN 8u
typedef void mutant_handle_copy_fn(void *to, const void *from);
typedef void mutant_handle_destroy_fn(void *handle);

/* The reallocation without the failure report, with the reads as given: reload_copy_end reads
   the end before every copy, snapshot keeps the begin and end read before the copies, free_early
   frees with the begin and allocator read before the destruction, destroy whether the old records
   are destroyed. */
static void mutant_handle_records(struct mutant_resize_array *array, uint64_t capacity, int reload_copy_end,
                                  int snapshot, int free_early, int destroy)
{
    mutant_handle_copy_fn *copy = (mutant_handle_copy_fn *)rt_guest(MUTANT_HANDLE_COPY);
    mutant_handle_destroy_fn *destroy_handle = (mutant_handle_destroy_fn *)rt_guest(MUTANT_HANDLE_DESTROY);
    const struct mutant_resize_allocator_vtable *vtable =
        *(const struct mutant_resize_allocator_vtable **)array->allocator;
    uint8_t *buffer = vtable->allocate(array->allocator, capacity * MUTANT_HANDLE_RECORD, MUTANT_HANDLE_RECORD_ALIGN);
    uint8_t *first = array->begin;
    uint8_t *last = array->end;
    uint8_t *to = buffer;
    for (uint8_t *from = first; from != (reload_copy_end ? array->end : last); from += MUTANT_HANDLE_RECORD) {
        if (to) {
            copy(to, from);
            *(uint32_t *)(to + MUTANT_HANDLE_RECORD_VALUE) = *(const uint32_t *)(from + MUTANT_HANDLE_RECORD_VALUE);
        }
        to += MUTANT_HANDLE_RECORD;
    }
    uint8_t *begin = snapshot ? first : array->begin;
    uint8_t *end = snapshot ? last : array->end;
    void *allocator = array->allocator;
    if (begin) {
        if (destroy)
            for (uint8_t *record = begin; record != end; record += MUTANT_HANDLE_RECORD)
                destroy_handle(record);
        const struct mutant_resize_allocator_vtable *free_vtable;
        if (free_early) {
            free_vtable = *(const struct mutant_resize_allocator_vtable **)allocator;
            free_vtable->free(allocator, begin);
        } else {
            free_vtable = *(const struct mutant_resize_allocator_vtable **)array->allocator;
            free_vtable->free(array->allocator, array->begin);
        }
    }
    uint64_t count = (uint64_t)(end - begin) / MUTANT_HANDLE_RECORD;
    array->capacity_end = buffer + capacity * MUTANT_HANDLE_RECORD;
    array->end = buffer + count * MUTANT_HANDLE_RECORD;
    array->begin = buffer;
}

/* Keeps the begin and end read before the copies: wrong for
   reallocates_array_of_24_byte_handle_records, which reads them again after the copies. */
void bb_mutant_handle_records_snapshot_ends(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 1, 0, 1);
}

/* Reads the end before every copy: wrong for reallocates_array_of_24_byte_handle_records, which
   copies up to the end read before the first. */
void bb_mutant_handle_records_reload_copy_end(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 1, 0, 0, 1);
}

/* Frees with the begin and allocator read before the destruction: wrong for
   reallocates_array_of_24_byte_handle_records, which reads both again after it. */
void bb_mutant_handle_records_free_early(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 0, 1, 1);
}

/* Frees the old buffer without destroying its records: wrong for
   reallocates_array_of_24_byte_handle_records. */
void bb_mutant_handle_records_without_destroy(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 0, 0, 0);
}

#define MUTANT_HANDLE_ASSIGN 0x02fa7bc0u
typedef void mutant_handle_assign_fn(void *to, const void *from);
/* The iterator erases_range_of_24_byte_handle_records takes by value and returns: the record
   pointer at +0x10. */
struct mutant_record_iterator {
    uint64_t unknown_0x0;
    uint64_t unknown_0x8;
    uint8_t *record;
};

/* The range erase with the reads as given: reload_assign_end reads the end before every
   assignment, snapshot_end destroys up to the end read before the assignments, whole_snapshot
   sets a wholly erased array's end to the begin read first, destroy whether records are
   destroyed. */
static struct mutant_record_iterator *mutant_erase_records(struct mutant_record_iterator *result,
                                                           struct mutant_resize_array *array,
                                                           const struct mutant_record_iterator *first,
                                                           const struct mutant_record_iterator *last,
                                                           int reload_assign_end, int snapshot_end, int whole_snapshot,
                                                           int destroy)
{
    mutant_handle_assign_fn *assign = (mutant_handle_assign_fn *)rt_guest(MUTANT_HANDLE_ASSIGN);
    mutant_handle_destroy_fn *destroy_handle = (mutant_handle_destroy_fn *)rt_guest(MUTANT_HANDLE_DESTROY);
    uint8_t *begin = array->begin;
    if (first->record == begin && last->record == array->end) {
        uint8_t *new_end = begin;
        if (begin != last->record) {
            if (destroy)
                for (uint8_t *record = begin; record != last->record; record += MUTANT_HANDLE_RECORD)
                    destroy_handle(record);
            new_end = whole_snapshot ? begin : array->begin;
        }
        array->end = new_end;
        result->record = begin;
        return result;
    }
    if (first->record != last->record) {
        uint8_t *end = array->end;
        uint8_t *kept_end = first->record;
        uint8_t *destroy_end = last->record;
        if (last->record != end) {
            for (uint8_t *from = last->record; from != (reload_assign_end ? array->end : end);
                 from += MUTANT_HANDLE_RECORD, kept_end += MUTANT_HANDLE_RECORD) {
                assign(kept_end, from);
                *(uint32_t *)(kept_end + MUTANT_HANDLE_RECORD_VALUE) = *(const uint32_t *)(from + MUTANT_HANDLE_RECORD_VALUE);
            }
            destroy_end = snapshot_end ? end : array->end;
        }
        if (destroy)
            for (uint8_t *record = kept_end; record != destroy_end; record += MUTANT_HANDLE_RECORD)
                destroy_handle(record);
        array->end = kept_end;
    }
    result->record = first->record;
    return result;
}

/* Reads the end before every assignment: wrong for erases_range_of_24_byte_handle_records,
   which moves the records up to the end read before the first. */
struct mutant_record_iterator *bb_mutant_erase_records_reload_assign_end(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 1, 0, 0, 1);
}

/* Destroys up to the end read before the assignments: wrong for
   erases_range_of_24_byte_handle_records, which reads it again after them. */
struct mutant_record_iterator *bb_mutant_erase_records_snapshot_end(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 1, 0, 1);
}

/* Sets a wholly erased array's end to the begin read first: wrong for
   erases_range_of_24_byte_handle_records, which reads the begin again after the destruction. */
struct mutant_record_iterator *bb_mutant_erase_records_whole_snapshot_begin(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 0, 1, 1);
}

/* Moves the records but destroys none: wrong for erases_range_of_24_byte_handle_records. */
struct mutant_record_iterator *bb_mutant_erase_records_without_destroy(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 0, 0, 0);
}
