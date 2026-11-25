// SPDX-License-Identifier: GPL-2.0-or-later
// Quiet scalar float comparisons matching vucomiss/ucomiss (STYLE.md: intrinsics only in
// game/engine/). Plain C++ `a > b` can compile to signaling comiss or vectorized cmpnltps, which
// raises the MXCSR invalid flag on a quiet NaN where vucomiss leaves it clear.
#pragma once

#include <immintrin.h>

namespace engine {

// Quiet scalar ordered greater-than (ucomiss; false for unordered/NaN without signaling invalid
// on quiet NaNs).
inline bool quiet_greater(float a, float b)
{
    return _mm_ucomigt_ss(_mm_set_ss(a), _mm_set_ss(b)) != 0;
}

}  // namespace engine
