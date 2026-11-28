// SPDX-License-Identifier: GPL-2.0-or-later
// The allocator interface constructor publishes its base dispatch table.
#include "yebis_allocator.h"

namespace {

RT_GLOBAL(0x056b3630, allocator_interface_vtable, const yebis::AllocatorVtablePrefix);

}  // namespace

extern "C" void bb_render_yebis_initialize_allocator_interface(yebis::AllocatorInterface *allocator)
{
    allocator->vtable = allocator_interface_vtable.address();
}
