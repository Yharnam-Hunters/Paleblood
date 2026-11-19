// SPDX-License-Identifier: GPL-2.0-or-later
// frees_block_at_0xa0_then_destroys_base_0x00569a90 (symbols/families.csv): the copies of one
// destructor body. When the object holds a block at +0xa0, the data the block's first field
// points at and then the block itself go to the free slot (0x68) of the allocator the global
// 0x057e7320 points at; the block field and the global are read again for the second free. Then
// the base destructor (0x00569a90) runs on the object. Which classes these are is not traced here.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct GlobalAllocator;
struct GlobalAllocatorVtable {
    void *unknown_slots[13];
    void (*free)(GlobalAllocator *, void *data);
};
static_assert(offsetof(GlobalAllocatorVtable, free) == 0x68);
struct GlobalAllocator {
    const GlobalAllocatorVtable *vtable;
};
RT_GLOBAL(0x057e7320, global_allocator, GlobalAllocator *);

struct Block {
    void *data;
};

struct BlockOwner {
    uint8_t unknown_0x00[0xa0];
    Block *block;
};
static_assert(offsetof(BlockOwner, block) == 0xa0);

RT_ORIGINAL(0x00569a90, destroy_base, void(BlockOwner *));

void free_through_global(void *data)
{
    GlobalAllocator *allocator = global_allocator.get();
    allocator->vtable->free(allocator, data);
}

}  // namespace

extern "C" void bb_engine_frees_block_at_0xa0_then_destroys_base_0x00569a90(BlockOwner *owner)
{
    if (Block *block = owner->block) {
        free_through_global(block->data);
        free_through_global(owner->block);
    }
    destroy_base(owner);
}
