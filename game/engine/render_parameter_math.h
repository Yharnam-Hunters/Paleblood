// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar conversions used by render parameter construction; preserve instruction rounding.
#pragma once

#include "scalar.h"

namespace engine {
__attribute__((target("sse4.1"))) inline float round_down(float x)
{
    return _mm_cvtss_f32(_mm_round_ss(_mm_set_ss(x), _mm_set_ss(x), _MM_FROUND_TO_NEG_INF));
}

inline float signed32_to_float(int32_t x)
{
    return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), x));
}
}  // namespace engine
