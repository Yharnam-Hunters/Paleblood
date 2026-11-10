// SPDX-License-Identifier: GPL-2.0-or-later
// Shared scalar parameter operations used by normalization and record construction.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x02ab7ab0, combine_record_parameters,
            uint32_t(uint32_t first, uint32_t second, uint32_t flag));
RT_ORIGINAL(0x02ab7e40, reduce_record_parameter, uint32_t(uint32_t value));
}  // namespace render
