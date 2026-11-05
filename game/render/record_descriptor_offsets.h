// SPDX-License-Identifier: GPL-2.0-or-later
// Geometry descriptors and aggregate sizes derived from packed records. Lower
// helpers retain their partial-output and ignored-status behavior.
#pragma once

#include <cstdint>

#include "record_descriptor.h"

namespace render {
RT_ORIGINAL(0x014a1e80, initialize_record_level_descriptor,
            uint32_t(RecordGeometryDescriptor *output, const RecordDescriptorPrefix *record,
                     uint32_t level, uint32_t index));
}  // namespace render
