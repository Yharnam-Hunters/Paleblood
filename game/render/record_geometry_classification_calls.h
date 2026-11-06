// SPDX-License-Identifier: GPL-2.0-or-later
// Decode the category and classification fields consumed by descriptor conversion.
#pragma once
#include "record_geometry_status.h"
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x0149ea90, get_record_geometry_category, uint32_t(uint32_t *output, uint32_t word));
RT_ORIGINAL(0x0149eac0, get_record_geometry_classification, uint32_t(uint32_t *output, uint32_t word));
}  // namespace render
