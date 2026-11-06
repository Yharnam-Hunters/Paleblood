// SPDX-License-Identifier: GPL-2.0-or-later
// Shared geometry calculation and exact-width total read used by constructors.
#pragma once
#include <cstdint>
#include "byte_field.h"
#include "record_geometry.h"
#include "runtime/original.h"
namespace render {
inline uint64_t read_geometry_total(const GeometryTotal &total)
{
    return Field<uint64_t>::read(total.bytes);
}

RT_ORIGINAL(0x0149ce10, calculate_record_geometry,
            uint32_t(RecordGeometryResult *output, const RecordGeometryDescriptor *descriptor));
}  // namespace render
