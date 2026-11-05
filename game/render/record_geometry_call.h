// SPDX-License-Identifier: GPL-2.0-or-later
// Geometry calculation used while constructing packed presentation records.
#pragma once
#include <cstdint>
#include "byte_field.h"
#include "record_geometry.h"
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x0149ce10, calculate_record_geometry,
            uint32_t(RecordGeometryResult *output, const RecordGeometryDescriptor *descriptor));
}  // namespace render
