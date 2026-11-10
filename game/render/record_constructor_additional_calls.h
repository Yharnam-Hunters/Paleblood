// SPDX-License-Identifier: GPL-2.0-or-later
// Secondary auxiliary construction and selector packing used by record_constructor.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x0149e400, record_constructor_optional_second,
            uint32_t(uint64_t *total, uint32_t *unit, uint32_t *first, uint32_t *second,
                     uint32_t first_value, uint32_t second_value, uint32_t count,
                     uint32_t selector, uint32_t encoding));
RT_ORIGINAL(0x01471600, record_constructor_pack_pair,
            uint32_t(uint32_t first_selector, uint32_t second_selector));
}  // namespace render
