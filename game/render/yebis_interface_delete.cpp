// SPDX-License-Identifier: GPL-2.0-or-later
// Restore the base interface vtable, check its fields and delete the storage.
#include "yebis_interface_release.h"

namespace {

RT_GLOBAL(0x056b35e0, interface_vtable, const yebis::InterfaceVtablePrefix);
RT_GLOBAL(0x04bfeb4b, destroy_count_message, const char);
RT_GLOBAL(0x04bfebba, destroy_instance_message, const char);
RT_GLOBAL(0x04bfebab, destroy_function_name, const char);
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

}  // namespace

extern "C" void bb_render_yebis_delete_interface(yebis::InterfaceStorage *object)
{
    object->vtable = interface_vtable.address();
    if (object->reference_count != 0)
        yebis::c_assert(destroy_count_message.address(), destroy_function_name.address());

    // A returning assertion may have changed the instance pointer.
    if (object->instance != nullptr)
        yebis::c_assert(destroy_instance_message.address(), destroy_function_name.address());

    operator_delete(object);
}
