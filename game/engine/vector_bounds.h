// SPDX-License-Identifier: GPL-2.0-or-later
// More Vec4 operations (vector.h has the rest), each the single instruction the original uses:
// bitwise xor, lane-wise minimum and maximum (bounds), and one value in all four lanes.
#pragma once

#include <immintrin.h>

#include "vector.h"

namespace engine {

// The lanes' bits xored (with a sign mask: every sign flipped, NaNs included).
inline Vec4 bitwise_xor(Vec4 a, Vec4 b) { return Vec4(_mm_xor_ps(a.v, b.v)); }

// Lane-wise minimum and maximum: where a lane is unordered (NaN) or equal, b's lane wins.
inline Vec4 lane_min(Vec4 a, Vec4 b) { return Vec4(_mm_min_ps(a.v, b.v)); }
inline Vec4 lane_max(Vec4 a, Vec4 b) { return Vec4(_mm_max_ps(a.v, b.v)); }

// The same value in all four lanes.
inline Vec4 splat(float value) { return Vec4(_mm_set1_ps(value)); }

}  // namespace engine
