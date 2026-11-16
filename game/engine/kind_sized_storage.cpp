// SPDX-License-Identifier: GPL-2.0-or-later
// Three families of methods on one kind of engine storage object (symbols/families.csv), each the
// copies of one function. The object holds a buffer of `count` four-byte entries followed by its
// elements; a virtual method (slot 0x88, called with 0) reports the object's kind, whose low bit
// is ignored. Kind 4 has three elements per entry; kind 6 has three per entry and then two more;
// any other kind has one element per entry. The kind
// is asked again (a second call) when the first answer is not kind 4.
//  - frees_kind_sized_vec4_storage_through_0x05ac8288: hands the buffer (16-byte elements after
//    the entries rounded up to 16 bytes) to the sized free of the allocator a global points at;
//  - allocates_kind_sized_dword_storage_through_0x05ac8288: allocates the buffer (four-byte
//    elements right after the entries, alignment 4) from that allocator and points the element
//    pointer past the entries; false when the allocation fails;
//  - sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288: stores the count first.
// The allocator is read before the kind is asked; the count after.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct KindSizedStorage;

struct KindSizedStorageVtable {
    void *unknown_slots[17];
    int32_t (*kind)(KindSizedStorage *, int32_t);
};
static_assert(offsetof(KindSizedStorageVtable, kind) == 0x88);

struct KindSizedStorage {
    const KindSizedStorageVtable *vtable;
    void *storage;
    void *elements;
    int32_t count;
};
static_assert(offsetof(KindSizedStorage, storage) == 0x8 && offsetof(KindSizedStorage, elements) == 0x10);
static_assert(offsetof(KindSizedStorage, count) == 0x18);

RT_ORIGINAL(0x02fa1b80, allocate_from, void *(void *allocator, uint64_t size, uint64_t alignment));
RT_ORIGINAL(0x02fa1c20, sized_free, void(void *allocator, void *data, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, storage_allocator, void *const);

constexpr int32_t kind_argument = 0;
constexpr uint32_t kind_ignored_bit = 1;
constexpr uint32_t kind_three_per_entry = 4;
constexpr uint32_t kind_three_per_entry_and_two = 6;
constexpr uint32_t elements_per_entry = 3;
constexpr uint32_t extra_elements = 2;
constexpr uint64_t entry_size = 4;
constexpr uint64_t dword_element_size = 4;
constexpr uint64_t dword_alignment = 4;
constexpr uint64_t vec4_size = 16;
constexpr uint64_t vec4_alignment = 16;

uint32_t kind_of(KindSizedStorage *object)
{
    return static_cast<uint32_t>(object->vtable->kind(object, kind_argument)) & ~kind_ignored_bit;
}

// The count and the number of elements, both read after the kind (in 32-bit arithmetic).
struct Layout {
    int32_t count;
    int32_t elements;
};

Layout layout_of(KindSizedStorage *object)
{
    if (kind_of(object) == kind_three_per_entry) {
        const int32_t count = object->count;
        return {count, static_cast<int32_t>(static_cast<uint32_t>(count) * elements_per_entry)};
    }
    const uint32_t kind = kind_of(object);
    const int32_t count = object->count;
    if (kind == kind_three_per_entry_and_two)
        return {count, static_cast<int32_t>(static_cast<uint32_t>(count) * elements_per_entry + extra_elements)};
    return {count, count};
}

uint64_t bytes_of(int32_t number, uint64_t size)
{
    return static_cast<uint64_t>(static_cast<int64_t>(number)) * size;
}

bool allocate_storage(KindSizedStorage *object)
{
    void *allocator = storage_allocator.get();
    const Layout layout = layout_of(object);
    const uint64_t entries = bytes_of(layout.count, entry_size);
    void *storage = allocate_from(allocator, bytes_of(layout.elements, dword_element_size) + entries, dword_alignment);
    if (!storage)
        return false;
    object->storage = storage;
    object->elements = static_cast<uint8_t *>(storage) + entries;
    return true;
}

}  // namespace

extern "C" void bb_engine_frees_kind_sized_vec4_storage_through_0x05ac8288(KindSizedStorage *object)
{
    void *allocator = storage_allocator.get();
    const Layout layout = layout_of(object);
    const uint64_t entries = (bytes_of(layout.count, entry_size) + vec4_alignment - 1) & ~(vec4_alignment - 1);
    sized_free(allocator, object->storage, bytes_of(layout.elements, vec4_size) + entries, vec4_alignment);
}

extern "C" bool bb_engine_allocates_kind_sized_dword_storage_through_0x05ac8288(KindSizedStorage *object)
{
    return allocate_storage(object);
}

extern "C" bool bb_engine_sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288(
    KindSizedStorage *object, int32_t count)
{
    object->count = count;
    return allocate_storage(object);
}

