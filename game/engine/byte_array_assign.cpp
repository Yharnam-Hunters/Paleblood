// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the copy assignment of the engine's byte array (container.h), a template instance the
// load request's copy uses for its data (loading/resource_queue.h).
//
// engine_byte_array_assign_02b4d100 (0x02b4d100) makes `array` hold the source's bytes and returns
// it. Assigning an array to itself does nothing. Otherwise it first calls slot 0x10 of the array's
// allocator, then of the source's (results ignored, as in the wide string's assignment), then:
// - an empty source empties the array and keeps its buffer;
// - a source no longer than the array is moved over its first bytes (memmove);
// - one that fits in the array's capacity is moved over all of its bytes, and the rest is copied
//   after them;
// - a larger one frees the array's buffer (if any), takes a new one of exactly the source's size
//   (alignment 1) and copies into it. A failed allocation goes to the engine's transaction handling
//   (transaction.h); outside a transaction the array goes on with a null buffer.
// The copies after a move construct the bytes in place one at a time, and that construction skips
// a null address: with a null buffer the first byte is dropped and the next ones are written from
// address 1 on, as in the game.
#include <cstdint>

#include "allocator.h"
#include "container.h"
#include "runtime/original.h"
#include "transaction.h"
#include "wide_string_assign.h"

namespace {

using ByteArray = engine::Array<uint8_t>;

RT_ORIGINAL(0x02fbe658, c_memmove, void *(void *, const void *, uint64_t));

// The alignment the byte array asks its allocator for.
constexpr uint64_t byte_alignment = 1;

// `address + count` without the null pointer's rule: a failed allocation goes on from address 0.
uint8_t *advance(uint8_t *address, uint64_t count)
{
    return reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(address) + count);
}

// Copies [first, last) to `out` a byte at a time as in-place construction does: a slot whose
// address is null is skipped. Returns the end of the copy.
uint8_t *construct_copy(const uint8_t *first, const uint8_t *last, uint8_t *out)
{
    uintptr_t slot = reinterpret_cast<uintptr_t>(out);
    for (const uint8_t *in = first; in != last; ++in, ++slot)
        if (slot != 0)
            *reinterpret_cast<uint8_t *>(slot) = *in;
    return reinterpret_cast<uint8_t *>(slot);
}

// The array holds a new buffer of exactly the source's bytes.
void replace_buffer(ByteArray *array, const ByteArray *source)
{
    if (array->begin)
        array->allocator->free(array->begin);
    array->capacity_end = nullptr;
    array->end = nullptr;
    array->begin = nullptr;

    const uint64_t size = source->count();
    if (size == 0)
        return;
    auto *buffer = static_cast<uint8_t *>(array->allocator->allocate(size, byte_alignment));
    if (!buffer)
        engine::report_failed_allocation();
    array->begin = buffer;
    array->end = buffer;
    array->capacity_end = advance(buffer, size);
    array->end = construct_copy(source->begin, source->end, buffer);
}

}  // namespace

extern "C" ByteArray *bb_engine_byte_array_assign_02b4d100(ByteArray *array, const ByteArray *source)
{
    if (array == source)
        return array;
    engine::Allocator *source_allocator = source->allocator;
    engine::query_allocator_slot_0x10(array->allocator);
    engine::query_allocator_slot_0x10(source_allocator);

    const uint64_t size = source->count();
    const uint64_t old_size = array->count();
    const uint64_t capacity = array->capacity_end - array->begin;
    if (size == 0) {
        array->end = array->begin;
    } else if (size <= old_size) {
        c_memmove(array->begin, source->begin, size);
        array->end = array->begin + source->count();
    } else if (size <= capacity) {
        const uint8_t *from = source->begin;
        c_memmove(array->begin, from, old_size);
        array->end = construct_copy(from + old_size, source->end, array->end);
    } else {
        replace_buffer(array, source);
    }
    return array;
}
