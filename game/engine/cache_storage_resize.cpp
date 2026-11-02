// SPDX-License-Identifier: GPL-2.0-or-later
// Resize the effect collector's opaque owner storage. The observed caller ignores
// the return register; the shared allocator interface remains opaque here.
#include "cache_storage.h"

extern "C" void bb_engine_resize_cache_storage(void *owner, uint32_t byte_count)
{
    using namespace engine::cache;
    void *previous_storage = read_storage(owner);
    if (previous_storage != nullptr) {
        engine::Allocator *allocator = read_allocator(owner);
        const auto release = release_method(allocator);
        release(allocator, previous_storage);
        // The callback sees the old field. Clear only after it returns, and only
        // on this path; a zero/zero request never accesses the allocator.
        publish_storage(owner, nullptr);
    }
    if (byte_count != 0) {
        // The release callback can change the allocator or its vptr through a
        // legitimate alias. Both loads must happen again before allocation.
        engine::Allocator *allocator = read_allocator(owner);
        const auto allocate = allocation_method(allocator);
        void *storage = allocate(allocator, byte_count, storage_alignment);
        publish_storage(owner, storage);
    }
}
