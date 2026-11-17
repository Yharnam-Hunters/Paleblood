// SPDX-License-Identifier: GPL-2.0-or-later
// Eight families (symbols/families.csv), each the copies of one function:
//  - thunk_minus_0x20_destroys_with_0x005696e0_then_frees: an adjusting entry that moves the
//    pointer back 0x20 bytes to the whole object, destroys it with 0x005696e0, then hands it to
//    the free (slot 0x68) of the allocator the global 0x057e7320 points at;
//  - queries_interface_0x04b2f0f0_or_0x04b2f320: compare the asked 16-byte key with two keys;
//    on either, take a reference (slot 0x8) and store the object; only the first answers 0;
//  - copies_entry_found_by_key_at_0x30 / stores_entry_by_key_at_0x30: look a 16-byte key up in
//    the tree at +0x30 (end node at +0x38) and copy out, or store, the entry's data pointer and
//    size; storing the key 0x04b2ed90 also keeps the data pointer at +0x18;
//  - frees_layout_block_align_16 / frees_layout_block_align_4 /
//    allocates_layout_block_for_stored_count / allocates_layout_block_for_count: free or
//    allocate an object's block through the allocator the global 0x05ac8288 points at, sized by
//    the element count the layout kind (slot 0x88) gives.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

// ---- the adjusting deleting entry -----------------------------------------------------------

struct Allocator;
using Free = uint64_t (*)(Allocator *allocator, void *object);
struct Allocator {
    const Free *vtable;
};
constexpr std::size_t free_slot = 0x68;
constexpr std::ptrdiff_t subobject_offset = 0x20;

RT_ORIGINAL(0x005696e0, destructor_005696e0, void(void *object));
RT_GLOBAL(0x057e7320, object_allocator, Allocator *const);

// ---- interface query and the keyed store ----------------------------------------------------

constexpr uint64_t key_size = 0x10;
constexpr uint32_t first_interface_key = 0x04b2f0f0;
constexpr uint32_t second_interface_key = 0x04b2f320;
constexpr uint32_t default_entry_key = 0x04b2ed90;
constexpr uint32_t no_interface = 0x80000004;

struct Interface;
using AddReference = void (*)(Interface *object);
struct InterfaceTable {
    uint8_t unknown_0x0[0x8];
    AddReference add_reference;
};
static_assert(offsetof(InterfaceTable, add_reference) == 0x8);
struct Interface {
    const InterfaceTable *vtable;
};

struct Node {
    uint8_t unknown_0x0[0x20];
    const void *data;
    uint32_t size;
};
static_assert(offsetof(Node, data) == 0x20);
static_assert(offsetof(Node, size) == 0x28);

struct Tree {
    uint8_t unknown_0x0[0x8];
    Node *end;
};

struct KeyedStore {
    uint8_t unknown_0x0[0x18];
    const void *default_data;
    uint8_t unknown_0x20[0x10];
    Tree tree;
};
static_assert(offsetof(KeyedStore, default_data) == 0x18);
static_assert(offsetof(KeyedStore, tree) == 0x30);

struct Iterator {
    uint8_t unknown_0x0[0x10];
    Node *node;
};

struct Entry {
    uint8_t key[key_size];
    const void *data;
    uint32_t size;
};
static_assert(offsetof(Entry, data) == 0x10);
static_assert(offsetof(Entry, size) == 0x18);

RT_ORIGINAL(0x02fbe638, compare_bytes, int32_t(const void *left, const void *right, uint64_t count));
RT_ORIGINAL(0x02fbe578, copy_bytes, void *(void *destination, const void *source, uint64_t count));
RT_ORIGINAL(0x02e3b0f0, find_entry, void(Iterator *found, Tree *tree, const void *key));
RT_ORIGINAL(0x02e3b210, insert_entry, void(Iterator *inserted, Tree *tree, const Entry *entry));

bool same_key(const void *key, uint32_t known_key)
{
    return compare_bytes(key, rt_guest(known_key), key_size) == 0;
}

// ---- the layout-sized block -----------------------------------------------------------------

struct LayoutObject;
using LayoutKind = int32_t (*)(LayoutObject *object, uint32_t part);
struct LayoutTable {
    uint8_t unknown_0x0[0x88];
    LayoutKind layout_kind;
};
static_assert(offsetof(LayoutTable, layout_kind) == 0x88);

struct LayoutObject {
    const LayoutTable *vtable;
    uint8_t *block;
    uint8_t *elements;
    int32_t count;
};
static_assert(offsetof(LayoutObject, block) == 0x8);
static_assert(offsetof(LayoutObject, elements) == 0x10);
static_assert(offsetof(LayoutObject, count) == 0x18);

constexpr int32_t kind_mask = ~1;
constexpr int32_t kind_triples = 4;
constexpr int32_t kind_triples_plus_two = 6;
constexpr uint32_t elements_per_count = 3;
constexpr uint32_t extra_elements = 2;
constexpr uint32_t whole_object = 0;
constexpr uint64_t element_bytes = 0x10;
constexpr uint64_t index_bytes = 4;
constexpr uint64_t block_alignment = 0x10;
constexpr uint64_t packed_alignment = 4;

RT_ORIGINAL(0x02fa1b80, allocate_aligned, uint8_t *(void *allocator, uint64_t size, uint64_t alignment));
RT_ORIGINAL(0x02fa1c20, free_sized, uint64_t(void *allocator, void *block, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, block_allocator, void *const);

int32_t layout_kind(LayoutObject *object)
{
    return object->vtable->layout_kind(object, whole_object) & kind_mask;
}

// The elements the layout kind asks for: three per count, three plus two, or one (int32
// arithmetic, as the original's lea). The count is read after the last kind query.
int32_t element_count(LayoutObject *object)
{
    if (layout_kind(object) == kind_triples)
        return static_cast<int32_t>(elements_per_count * static_cast<uint32_t>(object->count));
    const int32_t kind = layout_kind(object);
    const uint32_t count = static_cast<uint32_t>(object->count);
    if (kind == kind_triples_plus_two)
        return static_cast<int32_t>(elements_per_count * count + extra_elements);
    return static_cast<int32_t>(count);
}

// The 4-byte indices, rounded up to 16 bytes, that come before the 16-byte elements.
uint64_t index_area(int32_t count)
{
    return (static_cast<uint64_t>(static_cast<int64_t>(count)) * index_bytes + (block_alignment - 1)) &
           ~(block_alignment - 1);
}

uint64_t element_area(int32_t elements)
{
    return static_cast<uint64_t>(static_cast<int64_t>(elements)) * element_bytes;
}

bool allocate_layout_block(LayoutObject *object)
{
    void *allocator = block_allocator.get();
    const int32_t elements = element_count(object);
    const uint64_t indices = index_area(object->count);
    uint8_t *block = allocate_aligned(allocator, element_area(elements) + indices, block_alignment);
    if (!block)
        return false;
    object->block = block;
    object->elements = block + indices;
    return true;
}

}  // namespace

extern "C" uint64_t bb_engine_thunk_minus_0x20_destroys_with_0x005696e0_then_frees(void *subobject)
{
    void *object = static_cast<uint8_t *>(subobject) - subobject_offset;
    destructor_005696e0(object);
    Allocator *allocator = object_allocator.get();
    return allocator->vtable[free_slot / sizeof(Free)](allocator, object);
}

// The second key takes a reference and stores the object, yet answers no_interface, as the
// original does.
extern "C" uint32_t bb_engine_queries_interface_0x04b2f0f0_or_0x04b2f320(Interface *object, const void *key,
                                                                         Interface **out)
{
    uint32_t result = 0;
    if (!same_key(key, first_interface_key)) {
        result = no_interface;
        if (!same_key(key, second_interface_key))
            return result;
    }
    object->vtable->add_reference(object);
    *out = object;
    return result;
}

// 1 when the key is absent or *size is too small (then *size becomes the entry's size); else
// the entry's data is copied to data and the result is 0.
extern "C" uint32_t bb_engine_copies_entry_found_by_key_at_0x30(KeyedStore *store, const void *key, uint32_t *size,
                                                                void *data)
{
    Iterator found;
    find_entry(&found, &store->tree, key);
    Node *node = found.node;
    if (node == store->tree.end)
        return 1;
    if (*size < node->size) {
        *size = node->size;
        return 1;
    }
    copy_bytes(data, node->data, node->size);
    return 0;
}

extern "C" uint32_t bb_engine_stores_entry_by_key_at_0x30(KeyedStore *store, const void *key, uint32_t size,
                                                          const void *data)
{
    Iterator found;
    find_entry(&found, &store->tree, key);
    Node *node = found.node;
    if (node == store->tree.end) {
        Entry entry{};
        std::memcpy(entry.key, key, key_size);
        Iterator inserted;
        insert_entry(&inserted, &store->tree, &entry);
        node = inserted.node;
    }
    node->data = data;
    node->size = size;
    if (same_key(key, default_entry_key))
        store->default_data = data;
    return 0;
}

extern "C" bool bb_engine_frees_layout_block_align_16(LayoutObject *object)
{
    if (object->block) {
        void *allocator = block_allocator.get();
        const int32_t elements = element_count(object);
        free_sized(allocator, object->block, element_area(elements) + index_area(object->count), block_alignment);
    }
    object->elements = nullptr;
    object->block = nullptr;
    return true;
}

// The packed variant: elements and indices are both 4 bytes, with no rounding.
extern "C" bool bb_engine_frees_layout_block_align_4(LayoutObject *object)
{
    if (object->block) {
        void *allocator = block_allocator.get();
        const int32_t elements = element_count(object);
        const uint64_t size = (static_cast<uint64_t>(static_cast<int64_t>(elements)) +
                               static_cast<uint64_t>(static_cast<int64_t>(object->count))) * index_bytes;
        free_sized(allocator, object->block, size, packed_alignment);
    }
    object->elements = nullptr;
    object->block = nullptr;
    return true;
}

extern "C" bool bb_engine_allocates_layout_block_for_stored_count(LayoutObject *object)
{
    return allocate_layout_block(object);
}

extern "C" bool bb_engine_allocates_layout_block_for_count(LayoutObject *object, int32_t count)
{
    object->count = count;
    return allocate_layout_block(object);
}
