// SPDX-License-Identifier: GPL-2.0-or-later
// The reciprocal estimate instruction (rcpps): about 12 bits of 1 / x per lane. Its exact result
// is the processor's, so a replacement must use the same instruction to match the original.
#pragma once

#include "vector.h"

namespace engine {

inline Vec4 reciprocal_estimate(Vec4 value) { return Vec4(_mm_rcp_ps(value.v)); }

}  // namespace engine
