// SPDX-License-Identifier: GPL-2.0-or-later
// Optional constructor calculations use borrowed outputs and private geometry
// scratch. These declarations retain the observed scalar and publication widths.
#pragma once

#include <cstdint>

#include "record_geometry_call.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x0149e2d0, record_constructor_optional_first,
            uint32_t(uint64_t *total, uint32_t *unit, uint32_t *first, uint32_t *second,
                     uint32_t first_value, uint32_t second_value, uint32_t count, uint8_t flag));
}  // namespace render
