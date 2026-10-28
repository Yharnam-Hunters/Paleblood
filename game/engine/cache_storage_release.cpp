// SPDX-License-Identifier: GPL-2.0-or-later
// Release the owner's opaque storage through the shared allocator wire. The void
// procedure interface is inferred; no original return-register coverage is claimed.
#include "cache_storage.h"

extern "C" void bb_engine_release_cache_storage(void *owner)
{
    using namespace engine::cache;
    void *previous_storage = read_storage(owner);
    if (previous_storage != nullptr) {
        engine::Allocator *allocator = read_allocator(owner);
        const auto release = release_method(allocator);
        release(allocator, previous_storage);
        // Keep the old field visible until the opaque callback returns.
        publish_storage(owner, nullptr);
    }
}
