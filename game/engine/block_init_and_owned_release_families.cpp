// SPDX-License-Identifier: GPL-2.0-or-later
// Six engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one function.
//
// Three fill a caller's 0x80-byte block for an owner and return 0 (the copies of
// owner_block_families.cpp's kind with other fixed addresses and other counts of them):
// - initializes_owner_block_six_pointers_to_0x05ac5cac: that address in qwords 0-5, qwords 6-14
//   cleared;
// - initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc: one address each in qwords 0
//   and 1, qwords 2-14 cleared;
// - initializes_owner_block_first_pointer_to_0x05ac74a3: that address in qword 0, qwords 1-14
//   cleared.
// All keep the owner (rdi) in qword 15 (+0x78). Nothing else in the block is written and nothing
// is read from it.
//
// Three are destructors:
// - destroys_pair_owner_with_vtable_0x0570fb90: the class's vtable; only when there is an
//   object at +0x8: its release (vtable slot 0x18) and the field cleared, then the object at
//   +0x10, read after that, likewise when there is one; finally both fields cleared again (a
//   release can have stored into them). A missing first object leaves the second alone;
// - destroys_entry_storage_with_vtable_0x057045e0: the class's vtable, the dword at +0x20
//   cleared; unless the count at +0x24 is negative, the storage at +0x18 (16 bytes per entry, a
//   32-bit byte count) goes to slot 0x28 of the global allocator object at 0x058018b0; then the
//   storage pointer cleared and the count set to 0x80000000;
// - releases_counted_object_at_0x10: when there is an object at +0x10, pass it to 0x027286b0,
//   read the field again and, when it still holds one, drop that object's strong and weak counts
//   (destroy slot 0x18, free slot 0x20) and clear the field; then the empty base destructor
//   (which the replacement leaves out).
// Which classes these are is not traced here.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace {

// The owner blocks.
constexpr std::size_t block_qwords = 15;  // +0x0 up to +0x78
struct OwnerBlock {
    const void *qwords[block_qwords];
    void *owner;
};
static_assert(offsetof(OwnerBlock, owner) == 0x78);

// The addresses the block members store (only their addresses are used).
RT_GLOBAL(0x05ac5cac, block_address_six, const uint8_t);
RT_GLOBAL(0x05acd45c, block_address_first, const uint8_t);
RT_GLOBAL(0x05acd1bc, block_address_second, const uint8_t);
RT_GLOBAL(0x05ac74a3, block_address_one, const uint8_t);

constexpr std::size_t six_pointers = 6;
constexpr int32_t block_filled = 0;

int32_t fill(void *owner, OwnerBlock *block, const void *const *pointers, std::size_t count)
{
    for (std::size_t i = 0; i < block_qwords; ++i)
        block->qwords[i] = i < count ? pointers[i] : nullptr;
    block->owner = owner;
    return block_filled;
}

// destroys_pair_owner_with_vtable_0x0570fb90
struct OwnedObject;
struct OwnedObjectVtable {
    void *unknown_slots[3];
    void (*release)(OwnedObject *);
};
static_assert(offsetof(OwnedObjectVtable, release) == 0x18);
struct OwnedObject {
    const OwnedObjectVtable *vtable;
};

struct PairOwner {
    const uint8_t *vtable;
    OwnedObject *first;
    OwnedObject *second;
};
static_assert(offsetof(PairOwner, first) == 0x8 && offsetof(PairOwner, second) == 0x10);

RT_GLOBAL(0x0570fb90, pair_owner_vtable, const uint8_t);

void release(OwnedObject *object)
{
    object->vtable->release(object);
}

// destroys_entry_storage_with_vtable_0x057045e0
struct StorageAllocator;
struct StorageAllocatorVtable {
    uint8_t unknown_0x00[0x28];
    void (*free)(StorageAllocator *, void *storage, uint64_t bytes);
};
static_assert(offsetof(StorageAllocatorVtable, free) == 0x28);
struct StorageAllocator {
    const StorageAllocatorVtable *vtable;
};

struct EntryStorageOwner {
    const uint8_t *vtable;
    uint8_t unknown_0x08[0x10];
    void *storage;
    uint32_t value_0x20;
    int32_t count;
};
static_assert(offsetof(EntryStorageOwner, storage) == 0x18 && offsetof(EntryStorageOwner, value_0x20) == 0x20 &&
              offsetof(EntryStorageOwner, count) == 0x24);

RT_GLOBAL(0x057045e0, entry_storage_owner_vtable, const uint8_t);
RT_GLOBAL(0x058018b0, storage_allocator, StorageAllocator);
constexpr uint32_t entry_bytes_shift = 4;  // 16 bytes per entry
constexpr uint32_t storage_released = 0x80000000;

// releases_counted_object_at_0x10
struct Counted;
using CountedCall = void(Counted *);
struct CountedVtable {
    uint8_t unknown_0x00[0x18];
    CountedCall *destroy;
    CountedCall *free;
};
static_assert(offsetof(CountedVtable, destroy) == 0x18 && offsetof(CountedVtable, free) == 0x20);
// The counts: the strong count in bits 0-14, the weak count in bits 15-29, bit 30 alive.
struct Counted {
    const CountedVtable *vtable;
    uint32_t counts;
};
static_assert(offsetof(Counted, counts) == 0x8);
constexpr uint32_t count_mask = 0x7fff;
constexpr uint32_t weak_shift = 15;
constexpr uint32_t alive_bit = 0x40000000;
constexpr uint32_t above_strong = ~count_mask;
constexpr uint32_t keep_with_weak = 0xc0000000 | count_mask;
struct CountedOwner {
    uint8_t unknown_0x00[0x10];
    Counted *counted;
};
static_assert(offsetof(CountedOwner, counted) == 0x10);

RT_ORIGINAL(0x027286b0, before_counted_release, void(Counted *));

void drop_counts(Counted *counted)
{
    const uint32_t old = counted->counts;
    uint32_t counts = (old & above_strong) | ((old - 1) & count_mask);
    counted->counts = counts;
    if ((counts & count_mask) == 0 && (old & alive_bit)) {
        counted->vtable->destroy(counted);
        counts = counted->counts & ~alive_bit;
        counted->counts = counts;
    }
    const uint32_t weak = ((counts >> weak_shift) - 1) & count_mask;
    counted->counts = (counts & keep_with_weak) | (weak << weak_shift);
    if (weak == 0) {
        counted->counts = counts | (count_mask << weak_shift);
        counted->vtable->free(counted);
    }
}

}  // namespace

extern "C" int32_t bb_engine_initializes_owner_block_six_pointers_to_0x05ac5cac(void *owner, OwnerBlock *block)
{
    const void *pointers[six_pointers];
    for (const void *&pointer : pointers)
        pointer = block_address_six.address();
    return fill(owner, block, pointers, six_pointers);
}

extern "C" int32_t bb_engine_initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc(void *owner,
                                                                                            OwnerBlock *block)
{
    const void *const pointers[] = {block_address_first.address(), block_address_second.address()};
    return fill(owner, block, pointers, sizeof pointers / sizeof *pointers);
}

extern "C" int32_t bb_engine_initializes_owner_block_first_pointer_to_0x05ac74a3(void *owner, OwnerBlock *block)
{
    const void *const pointers[] = {block_address_one.address()};
    return fill(owner, block, pointers, sizeof pointers / sizeof *pointers);
}

extern "C" void bb_engine_destroys_pair_owner_with_vtable_0x0570fb90(PairOwner *owner)
{
    owner->vtable = pair_owner_vtable.address() + engine::vtable_address_point;
    if (!owner->first)
        return;
    release(owner->first);
    owner->first = nullptr;
    if (OwnedObject *second = owner->second) {
        release(second);
        owner->second = nullptr;
    }
    owner->second = nullptr;
    owner->first = nullptr;
}

extern "C" void bb_engine_destroys_entry_storage_with_vtable_0x057045e0(EntryStorageOwner *owner)
{
    owner->vtable = entry_storage_owner_vtable.address() + engine::vtable_address_point;
    owner->value_0x20 = 0;
    if (owner->count >= 0) {
        // The byte count is 32 bits (it wraps); the original passes it zero-extended.
        const uint32_t bytes = static_cast<uint32_t>(owner->count) << entry_bytes_shift;
        StorageAllocator *allocator = storage_allocator.address();
        allocator->vtable->free(allocator, owner->storage, bytes);
    }
    owner->storage = nullptr;
    owner->count = static_cast<int32_t>(storage_released);
}

extern "C" void bb_engine_releases_counted_object_at_0x10(CountedOwner *owner)
{
    if (!owner->counted)
        return;
    before_counted_release(owner->counted);
    if (Counted *counted = owner->counted) {
        drop_counts(counted);
        owner->counted = nullptr;
    }
}
