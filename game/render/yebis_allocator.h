// SPDX-License-Identifier: GPL-2.0-or-later
// YEBIS allocator callback slots share one dispatch prefix and allocation count.
#pragma once

#include "yebis_context.h"
#include "runtime/original.h"

namespace yebis {

struct AllocatorVtablePrefix {
    std::byte unknown_0x0000[0x18];
    void *(*allocate_storage)(AllocatorInterface *allocator, uint64_t bytes,
                             const char *source_file, uint32_t source_line,
                             const char *allocation_name);
    void (*free_storage)(AllocatorInterface *allocator, void *allocation);
};
static_assert(offsetof(AllocatorVtablePrefix, allocate_storage) == 0x18);
static_assert(offsetof(AllocatorVtablePrefix, free_storage) == 0x20);
static_assert(sizeof(AllocatorVtablePrefix) == 0x28);

RT_GLOBAL(0x05866700, allocation_count, uint32_t);

}  // namespace yebis
