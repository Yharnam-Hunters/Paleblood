// SPDX-License-Identifier: GPL-2.0-or-later
// Lane-wise minimum and maximum (bounds) of Vec4s, each the single instruction the original uses
// (vector.h and vector_operations.h have the rest).
#pragma once

#include <immintrin.h>

#include "vector.h"

namespace engine {

// Lane-wise minimum and maximum: where a lane is unordered (NaN) or equal, b's lane wins.
inline Vec4 lane_min(Vec4 a, Vec4 b) { return Vec4(_mm_min_ps(a.v, b.v)); }
inline Vec4 lane_max(Vec4 a, Vec4 b) { return Vec4(_mm_max_ps(a.v, b.v)); }

}  // namespace engine
