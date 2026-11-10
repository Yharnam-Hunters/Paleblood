// SPDX-License-Identifier: GPL-2.0-or-later
// Borrowed scalar outputs for auxiliary geometry calculations in the record builder.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x0149e2d0, record_constructor_optional_first,
            uint32_t(uint64_t *total, uint32_t *unit, uint32_t *first, uint32_t *second,
                     uint32_t first_value, uint32_t second_value, uint32_t count, uint8_t flag));
}  // namespace render
