// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize IPfxInterface before a derived YEBIS constructor continues.
#include "yebis_interface.h"
#include "runtime/original.h"

namespace {
RT_GLOBAL(0x056b35e0, interface_vtable, const yebis::InterfaceVtablePrefix);
}  // namespace

extern "C" void bb_render_yebis_initialize_interface(yebis::InterfaceStorage *object)
{
    object->vtable = interface_vtable.address();
    object->reference_count = 1;
    object->instance = nullptr;
}
