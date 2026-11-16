// SPDX-License-Identifier: GPL-2.0-or-later
// IPfxContext::Instantiate creates the context used by YEBIS initialization.
#include "yebis_context.h"
#include "runtime/original.h"

namespace {
constexpr uint64_t context_allocation_bytes = 0x178;
constexpr uint32_t context_factory_source_line = 629;

RT_GLOBAL(0x056b3618, current_allocator, yebis::AllocatorInterface *);
RT_GLOBAL(0x04bfc72a, allocation_source_file, const char);
RT_GLOBAL(0x04bfc75a, context_factory_name, const char);
RT_GLOBAL(0x056b3580, context_vtable, const void *);

// The source-line view preserves the caller-defined DWORD; its original
// signedness is unknown. The allocation size and all pointers are full width.
RT_ORIGINAL(0x00fb6f20, allocate_context,
            void *(yebis::AllocatorInterface *, uint64_t, const char *, uint32_t, const char *));
RT_ORIGINAL(0x00fb6d20, initialize_context_base, void(yebis::ContextStorage *));
RT_ORIGINAL(0x02a128b0, construct_gnm_shim, void(yebis::ContextGnmShimStorage *));
}  // namespace

extern "C" yebis::ContextStorage *bb_render_yebis_context_instantiate()
{
    auto *context = static_cast<yebis::ContextStorage *>(allocate_context(
        current_allocator.get(), context_allocation_bytes, allocation_source_file.address(),
        context_factory_source_line, context_factory_name.address()));

    // Allocation must provide writable context storage, as in the original.
    initialize_context_base(context);
    context->vtable = context_vtable.address();
    construct_gnm_shim(&context->gnm_shim);
    return context;
}
