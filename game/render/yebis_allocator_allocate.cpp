// SPDX-License-Identifier: GPL-2.0-or-later
// Dispatch YEBIS storage allocation and count each normally returning request.
#include <cstring>

#include "yebis_allocator_allocate.h"
#include "runtime/original.h"

namespace {

RT_GLOBAL(0x05866700, allocation_count, uint32_t);

}  // namespace

extern "C" void *bb_render_yebis_allocate_storage(
    yebis::AllocatorInterface *allocator, uint64_t bytes, const char *source_file,
    uint32_t source_line, const char *allocation_name)
{
    const auto *vtable = static_cast<const yebis::AllocatorAllocationVtablePrefix *>(allocator->vtable);
    void *allocation = vtable->allocate_storage(allocator, bytes, source_file, source_line,
                                               allocation_name);

    // The method may change the counter. Count its return even when it is null.
    void *count_storage = allocation_count.address();
    uint32_t count;
    std::memcpy(&count, count_storage, sizeof(count));
    ++count;
    std::memcpy(count_storage, &count, sizeof(count));
    return allocation;
}
