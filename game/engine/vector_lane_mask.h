// SPDX-License-Identifier: GPL-2.0-or-later
// Lane comparisons gathered into an integer: one bit per lane (x in bit 0), as movmskps gives
// them.
#pragma once

#include <cstdint>

#include "vector.h"

namespace engine {

// Bit n set where lane n of `a` <= lane n of `b` (false for NaN).
inline uint32_t less_equal_lanes(Vec4 a, Vec4 b)
{
    return static_cast<uint32_t>(_mm_movemask_ps(_mm_cmple_ps(a.v, b.v)));
}

}  // namespace engine
