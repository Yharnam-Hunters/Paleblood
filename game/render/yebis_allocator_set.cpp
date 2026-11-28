// SPDX-License-Identifier: GPL-2.0-or-later
// SetMemoryAllocator changes the YEBIS allocator only while the allocation count is zero.
#include "yebis_allocator.h"
#include "yebis_interface_release.h"

namespace {

constexpr uint32_t allocator_change_failure = 0x80004005;
constexpr int32_t allocator_change_source_line = 98;

RT_GLOBAL(0x05519a68, allocator_diagnostics_gate, uint32_t);
RT_GLOBAL(0x05519a70, allocator_diagnostics_detail_gate, uint32_t);
RT_GLOBAL(0x056b3610, default_allocator, yebis::AllocatorInterface);
RT_GLOBAL(0x04bfec19, allocator_change_source, const char);
RT_GLOBAL(0x04bfec6b, allocator_change_message, const char);
RT_GLOBAL(0x04bfecb7, allocator_change_assertion, const char);
RT_GLOBAL(0x04bfed27, allocator_change_function, const char);

RT_ORIGINAL(0x00fb1a90, log_message, int32_t(const char *format, ...));

}  // namespace

extern "C" uint32_t bb_render_yebis_set_allocator(yebis::AllocatorInterface *allocator)
{
    if (yebis::allocation_count.get() == 0) {
        yebis::current_allocator.get() = allocator ? allocator : default_allocator.address();
        return 0;
    }

    if (allocator_diagnostics_gate.get() != 0 && allocator_diagnostics_detail_gate.get() != 0) {
        log_message(allocator_change_source.address(), allocator_change_source_line);
        log_message(allocator_change_message.address());

        // Returning log callbacks may have cleared the allocation count. The
        // attempted change still fails, but a cleared count suppresses the assertion.
        if (yebis::allocation_count.get() == 0)
            return allocator_change_failure;
    }

    yebis::c_assert(allocator_change_assertion.address(), allocator_change_function.address());
    return allocator_change_failure;
}
