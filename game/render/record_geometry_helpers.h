// SPDX-License-Identifier: GPL-2.0-or-later
// Numeric fields used by the record geometry calculation. Table positions and
// bit widths are known; their physical resource meanings remain opaque.
#pragma once

#include <cstdint>

#include "record_geometry_classification_calls.h"
#include "record_geometry_rounding.h"
#include "runtime/original.h"

namespace render {
constexpr uint32_t record_geometry_field_entries = 32;
constexpr uint32_t record_geometry_parameter_entries = 13;
using RecordGeometryFields = uint32_t[record_geometry_field_entries];
using RecordGeometryParameterFields = uint32_t[record_geometry_parameter_entries];
RT_GLOBAL(0x04cd1790, record_geometry_fields, RecordGeometryFields);
RT_GLOBAL(0x04b2cba0, record_geometry_parameter_fields, RecordGeometryParameterFields);

RT_ORIGINAL(0x0149e920, record_geometry_index_logarithm, uint32_t(uint32_t value));
RT_ORIGINAL(0x0149eb20, get_record_geometry_first_exponent, uint32_t(uint32_t *output, uint32_t word));
RT_ORIGINAL(0x0149eb50, get_record_geometry_second_exponent, uint32_t(uint32_t *output, uint32_t word));
RT_ORIGINAL(0x0149c660, record_geometry_resolve_parameter_index,
            uint32_t(uint32_t *output, uint32_t word, uint32_t metadata, uint32_t count));
RT_ORIGINAL(0x0149ed30, get_record_geometry_parameters,
            uint32_t(uint32_t word, uint32_t metadata, uint32_t count,
                     uint32_t *first, uint32_t *second, uint32_t *third, uint32_t *fourth));
}  // namespace render
