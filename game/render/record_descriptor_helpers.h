// SPDX-License-Identifier: GPL-2.0-or-later
// Descriptor component values, geometry flags and aggregate size. The size
// helper has required outputs and retains partial publications on failure.
#pragma once

#include <cstdint>

#include "record_descriptor.h"

namespace render {
RT_ORIGINAL(0x01471a40, read_record_descriptor_component,
            uint32_t(const uint32_t *encoding, uint32_t component));
RT_ORIGINAL(0x0149c4e0, initialize_record_geometry_flags,
            uint32_t(uint32_t *output, uint32_t selector, uint32_t parameter));
RT_ORIGINAL(0x014a27c0, get_record_descriptor_size,
            uint32_t(uint64_t *total, uint32_t *unit, const RecordDescriptorPrefix *record));
}  // namespace render
