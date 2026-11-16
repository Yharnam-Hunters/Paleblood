// SPDX-License-Identifier: GPL-2.0-or-later
// IPfxInterface::Release drops a reference and cleans up the last one. The
// observed result is the cached DWORD count; its original C++ declaration is unknown.
#include "yebis_interface_release.h"

extern "C" uint32_t bb_render_yebis_release_interface(yebis::InterfaceStorage *object)
{
    uint32_t remaining = object->reference_count;
    if (remaining == 0) {
        yebis::c_assert(yebis::release_count_message.address(),
                       yebis::release_function_name.address());
        // The original continues after the assertion and reads the count again.
        remaining = object->reference_count;
    }

    --remaining;
    object->reference_count = remaining;
    if (remaining == 0) {
        object->vtable->cleanup(object);
        // Cleanup may replace the allocator; fetch it only after that call.
        yebis::free_interface_storage(yebis::current_allocator.get(), object);
    }
    return remaining;
}
