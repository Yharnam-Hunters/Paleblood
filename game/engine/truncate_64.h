// SPDX-License-Identifier: GPL-2.0-or-later
// 64-bit float truncation as the engine's compiler emits it (STYLE.md: intrinsics only in
// game/engine/).
#pragma once

#include <immintrin.h>

#include <cstdint>

namespace engine {

// cvttss2si to a 64-bit register: truncates toward zero; out of range or NaN gives INT64_MIN (a
// C++ cast would be undefined there). Code that then uses only the low 32 bits sees 0 for those.
inline int64_t truncate_64(float x) { return _mm_cvttss_si64(_mm_set_ss(x)); }

}  // namespace engine
