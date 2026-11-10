// SPDX-License-Identifier: GPL-2.0-or-later
// Read one of four three-bit fields from a captured encoding DWORD. An invalid
// component returns the native value two before touching the borrowed pointer.
#include <cstring>

#include "record_descriptor_helpers.h"

extern "C" uint32_t bb_render_read_record_descriptor_component(const uint32_t *encoding, uint32_t component)
{
    constexpr uint32_t component_count = 4, invalid_component = 2, component_mask = 7;
    constexpr unsigned first_shift = 12, component_width = 3;
    if (component >= component_count) return invalid_component;
    uint32_t captured;
    std::memcpy(&captured, encoding, sizeof(captured));
    return (captured >> (first_shift + component_width * component)) & component_mask;
}
