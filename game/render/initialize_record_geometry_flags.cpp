// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize exactly one borrowed DWORD. A non-NULL output is cleared before
// selector admission, so rejected selectors still publish zero.
#include <cstring>

#include "record_descriptor_helpers.h"
#include "record_geometry_status.h"

namespace {
constexpr uint32_t unsupported_flags = 0xffffffff;
// Indexed by selector. Selectors 1/2 clear without adding flags, 3 is unsupported;
// selectors 7–12 additionally copy the caller's unit-parameter flag.
constexpr uint32_t base_flags_by_selector[] = {
    unsupported_flags, 0, 0, unsupported_flags, 2, 4, 0x40,
    0, 0x20, 0x10, 0, 0x20, 0x10,
};
}  // namespace

extern "C" uint32_t bb_render_initialize_record_geometry_flags(
    uint32_t *output, uint32_t selector, uint32_t parameter)
{
    constexpr uint32_t selector_count = 12, first_selector = 1;
    constexpr uint32_t parameter_flag = 0x1000, first_parameter_selector = 7;
    if (output == nullptr) return render::record_geometry_unsupported;
    const uint32_t zero = 0;
    std::memcpy(output, &zero, sizeof(zero));
    if (selector - first_selector >= selector_count) return render::record_geometry_unsupported;

    const uint32_t base = base_flags_by_selector[selector];
    if (base == unsupported_flags) return render::record_geometry_unsupported;
    const uint32_t flag = selector >= first_parameter_selector && parameter == 1 ? parameter_flag : 0;
    const uint32_t value = base | flag;
    std::memcpy(output, &value, sizeof(value));
    return 0;
}
