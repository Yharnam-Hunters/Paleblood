// SPDX-License-Identifier: GPL-2.0-or-later
// Release linked interface storage before the base interface destructor.
// The derived deleting slot and the ordinary interface cleanup slot reach here.
#include "yebis_linked_interface_cleanup.h"
#include "yebis_diagnostics.h"
#include "yebis_interface_release.h"

namespace {

constexpr int32_t cleanup_entry_source_line = 58;
constexpr int32_t cleanup_warning_source_line = 69;
constexpr int32_t cleanup_exit_source_line = 105;

RT_GLOBAL(0x05865eb0, linked_interface_diagnostics_gate, uint32_t);
RT_GLOBAL(0x04bfed3a, cleanup_source, const char);
RT_GLOBAL(0x04bfed86, cleanup_entry_message, const char);
RT_GLOBAL(0x04bfedc5, missing_other_instance_message, const char);
RT_GLOBAL(0x04bfee29, cleanup_function_name, const char);
RT_GLOBAL(0x04bfee42, cleanup_warning_source, const char);
RT_GLOBAL(0x04bfee94, cleanup_warning_message, const char);
RT_GLOBAL(0x04bfef3b, other_instance_busy_message, const char);
RT_GLOBAL(0x04bfefb2, cleanup_exit_message, const char);
RT_GLOBAL(0x04bff673, reference_count_message, const char);
RT_GLOBAL(0x04bff6bd, reference_release_function_name, const char);

}  // namespace

extern "C" void bb_render_yebis_cleanup_linked_interface(yebis::LinkedInterfaceStorage *object)
{
    object->interface.vtable = yebis::linked_interface_vtable.address();
    if (yebis::diagnostics_gate.get() != 0 && linked_interface_diagnostics_gate.get() != 0) {
        yebis::log_message(cleanup_source.address(), cleanup_entry_source_line);
        yebis::log_message(cleanup_entry_message.address());
    }

    const auto *other = object->other_interface;
    const uint8_t *other_instance = other ? static_cast<const uint8_t *>(other->instance) : nullptr;
    if (object->interface.instance != nullptr) {
        if (other_instance == nullptr)
            yebis::c_assert(missing_other_instance_message.address(), cleanup_function_name.address());

        // The byte pointer is cached. A missing pointer cannot be repaired by
        // a returning assertion; that original path subsequently faults.
        bool busy = *other_instance != 0;
        if (busy && yebis::diagnostics_gate.get() != 0 && yebis::diagnostics_detail_gate.get() != 0) {
            yebis::log_message(cleanup_warning_source.address(), cleanup_warning_source_line);
            yebis::log_message(cleanup_warning_message.address());
            busy = *other_instance != 0;
        }
        if (busy)
            yebis::c_assert(other_instance_busy_message.address(), cleanup_function_name.address());

        // Warning callbacks can replace or clear this field.
        auto *instance = static_cast<yebis::InterfaceStorage *>(object->interface.instance);
        if (instance != nullptr) {
            const uint32_t previous = instance->reference_count;
            uint32_t remaining = previous - 1;
            instance->reference_count = remaining;
            if (static_cast<int32_t>(previous) <= 0) {
                yebis::c_assert(reference_count_message.address(), reference_release_function_name.address());
                remaining = instance->reference_count;
            }

            if (remaining == 0) {
                // This routine saves the allocator before the virtual callback.
                auto *allocator = yebis::current_allocator.get();
                const auto *vtable = reinterpret_cast<const yebis::ReferencedInterfaceVtablePrefix *>(instance->vtable);
                vtable->on_last_reference(instance);
                yebis::free_interface_storage(allocator, instance);
            }
            object->interface.instance = nullptr;
        }
    }

    if (yebis::diagnostics_gate.get() != 0 && linked_interface_diagnostics_gate.get() != 0) {
        yebis::log_message(cleanup_source.address(), cleanup_exit_source_line);
        yebis::log_message(cleanup_exit_message.address());
    }
    yebis::destruct_interface(&object->interface);
}
