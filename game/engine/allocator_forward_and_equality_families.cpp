// SPDX-License-Identifier: GPL-2.0-or-later
// Seven engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one short
// function:
// - frees_8_byte_block_through_0x05ac8288: hand the block to the sized free (0x02fa1c20) of the
//   allocator the global 0x05ac8288 points at, size 8, alignment 8.
// - frees_sized_block_aligned_8_through_0x05ac8288: the same with the size given, alignment 8.
// - allocates_aligned_8_through_0x05ac8288: allocate (0x02fa1b80) the size given, alignment 8,
//   from that allocator; the block or null.
// - allocates_with_alignment_through_0x05ac8288: the same with the alignment given.
// - resets_with_0x02c375e0_and_returns_object: 0x02c375e0 with the object, then the object.
// - releases_value_at_0x8_with_0x005c14d0_then_clears_kind: release the value the object's +0x8
//   points at through 0x005c14d0 (which switches on the value's first byte, its kind), then clear
//   that byte. The value is read once, before the release.
// - equals_by_kind_and_16_bytes_at_0x10: two objects are equal when their slot 0 answers the same
//   kind (a byte; the second object's table is read after the first call) and the 16 bytes their
//   +0x10 points at (read after both calls) compare equal (memcmp, a 32-bit answer).
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

using Allocator = void;
RT_GLOBAL(0x05ac8288, engine_allocator, Allocator *const);
RT_ORIGINAL(0x02fa1b80, allocate, void *(Allocator *allocator, uint64_t size, uint64_t alignment));
RT_ORIGINAL(0x02fa1c20, sized_free, void(Allocator *allocator, void *block, uint64_t size, uint64_t alignment));

constexpr uint64_t block_alignment = 8;
constexpr uint64_t small_block_size = 8;

// resets_with_0x02c375e0_and_returns_object
RT_ORIGINAL(0x02c375e0, reset_object, void(void *object));

// releases_value_at_0x8_with_0x005c14d0_then_clears_kind
struct KindedValue {
    uint8_t kind;
};
struct ValueHolder {
    uint8_t unknown_0x00[0x8];
    KindedValue *value;
};
static_assert(offsetof(ValueHolder, value) == 0x8);
RT_ORIGINAL(0x005c14d0, release_value, void(KindedValue *value));
constexpr uint8_t released_kind = 0;

// equals_by_kind_and_16_bytes_at_0x10
struct KeyedObject;
struct KeyedObjectVtable {
    uint8_t (*kind)(KeyedObject *);
};
struct KeyedObject {
    const KeyedObjectVtable *vtable;
    uint8_t unknown_0x08[0x8];
    const uint8_t *key;
};
static_assert(offsetof(KeyedObject, key) == 0x10);
RT_ORIGINAL(0x02fbe638, c_memcmp, int32_t(const void *, const void *, uint64_t));
constexpr uint64_t key_size = 16;

}  // namespace

extern "C" void bb_engine_frees_8_byte_block_through_0x05ac8288(void *block)
{
    sized_free(engine_allocator.get(), block, small_block_size, block_alignment);
}

extern "C" void bb_engine_frees_sized_block_aligned_8_through_0x05ac8288(void *block, uint64_t size)
{
    sized_free(engine_allocator.get(), block, size, block_alignment);
}

extern "C" void *bb_engine_allocates_aligned_8_through_0x05ac8288(uint64_t size)
{
    return allocate(engine_allocator.get(), size, block_alignment);
}

extern "C" void *bb_engine_allocates_with_alignment_through_0x05ac8288(uint64_t size, uint64_t alignment)
{
    return allocate(engine_allocator.get(), size, alignment);
}

extern "C" void *bb_engine_resets_with_0x02c375e0_and_returns_object(void *object)
{
    reset_object(object);
    return object;
}

extern "C" void bb_engine_releases_value_at_0x8_with_0x005c14d0_then_clears_kind(ValueHolder *holder)
{
    KindedValue *value = holder->value;
    release_value(value);
    value->kind = released_kind;
}

extern "C" bool bb_engine_equals_by_kind_and_16_bytes_at_0x10(KeyedObject *object, KeyedObject *other)
{
    const uint8_t kind = object->vtable->kind(object);
    if (kind != other->vtable->kind(other))
        return false;
    return c_memcmp(object->key, other->key, key_size) == 0;
}
