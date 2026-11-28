// SPDX-License-Identifier: GPL-2.0-or-later
// YEBIS storage releases decrement the allocation count before allocator dispatch.
#include "yebis_allocator.h"

#include <cstring>

extern "C" void bb_render_yebis_free_storage(yebis::AllocatorInterface *allocator, void *allocation)
{
    void *count_storage = yebis::allocation_count.address();
    uint32_t count;
    // Representation access keeps the counter update ahead of possibly aliased
    // allocator storage without adding atomic or volatile semantics.
    std::memcpy(&count, count_storage, sizeof(count));
    --count;
    std::memcpy(count_storage, &count, sizeof(count));

    const auto *vtable = static_cast<const yebis::AllocatorVtablePrefix *>(allocator->vtable);
    vtable->free_storage(allocator, allocation);
}
