// SPDX-License-Identifier: GPL-2.0-or-later
// Run derived interface cleanup, then delete the original allocation pointer.
#include "yebis_linked_interface.h"
#include "../engine/fast_small_object_allocator.h"

extern "C" void bb_render_yebis_delete_linked_interface(yebis::InterfaceStorage *object)
{
    yebis::cleanup_linked_interface(object);
    engine::fast_small_object_allocator::operator_delete(object);
}
