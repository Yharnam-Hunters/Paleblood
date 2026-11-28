// SPDX-License-Identifier: GPL-2.0-or-later
// Run cleanup and free without directly adjusting the interface reference count.
#include "yebis_interface_release.h"

extern "C" void bb_render_yebis_cleanup_and_free_interface(yebis::InterfaceStorage *object)
{
    if (object == nullptr)
        return;

    object->vtable->cleanup(object);
    // Cleanup may replace the allocator; fetch it after that call.
    yebis::free_interface_storage(yebis::current_allocator.get(), object);
}
