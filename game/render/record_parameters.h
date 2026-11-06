// SPDX-License-Identifier: GPL-2.0-or-later
// Opaque parameter-code boundary used by the record normalizers. Output mutation and the
// meaning of the forwarded tuple remain owned by the original layout helper.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x0149c850, apply_record_parameter_tuple,
            uint32_t(uint32_t *output, uint32_t classification, uint32_t first,
                     uint32_t mapping, uint32_t parameter, uint32_t second));
}  // namespace render
