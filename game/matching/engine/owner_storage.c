// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the storage an owner obtains from its allocator.

#include <stdint.h>

#define STORAGE_ALIGNMENT 16

typedef struct Allocator Allocator;
typedef struct {
    uint8_t unknown_0x00[0x58];
    void *(*allocate)(Allocator *self, unsigned long size, unsigned long alignment);
    uint8_t unknown_0x60[0x10];
    void (*free)(Allocator *self, void *memory);
} AllocatorVtable;
struct Allocator {
    const AllocatorVtable *vtable;
};

typedef struct {
    uint8_t unknown_0x000[0x8];
    Allocator *allocator;
    uint8_t unknown_0x010[0x160];
    void *storage;
} StorageOwner;

// Frees the owner's storage, if any.
void engine_release_owner_storage(StorageOwner *owner)
{
    if (owner->storage) {
        owner->allocator->vtable->free(owner->allocator, owner->storage);
        owner->storage = 0;
    }
}

// Replaces the owner's storage with a new 16-byte-aligned block (none for a zero size).
void engine_resize_owner_storage(StorageOwner *owner, unsigned byte_count)
{
    if (owner->storage) {
        owner->allocator->vtable->free(owner->allocator, owner->storage);
        owner->storage = 0;
    }
    if (byte_count)
        owner->storage = owner->allocator->vtable->allocate(owner->allocator, byte_count,
                                                            STORAGE_ALIGNMENT);
}
