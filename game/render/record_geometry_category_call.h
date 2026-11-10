// SPDX-License-Identifier: GPL-2.0-or-later
// Decode the MICRO_TILE_MODE_NEW category of a GB_TILE_MODE table entry.
#pragma once
#include <cstdint>
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x0149ea90, get_record_geometry_category, uint32_t(uint32_t *output, uint32_t word));
}  // namespace render
