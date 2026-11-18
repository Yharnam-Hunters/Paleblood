// SPDX-License-Identifier: GPL-2.0-or-later
// Allocation dispatch prefix used by the YEBIS context factory's allocator.
#pragma once

#include "yebis_context.h"

namespace yebis {

struct AllocatorAllocationVtablePrefix {
    std::byte unknown_0x0000[0x18];
    void *(*allocate_storage)(AllocatorInterface *allocator, uint64_t bytes,
                              const char *source_file, uint32_t source_line,
                              const char *allocation_name);
};
static_assert(offsetof(AllocatorAllocationVtablePrefix, allocate_storage) == 0x18);
static_assert(sizeof(AllocatorAllocationVtablePrefix) == 0x20);

}  // namespace yebis
