// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar float operations as the engine's compiler emits them, where plain C++ would differ:
// each is the one instruction the original uses (STYLE.md: intrinsics only in game/engine/).
#pragma once

#include <immintrin.h>

#include <cstdint>

namespace engine {

// Rounds up (roundss toward +infinity).
__attribute__((target("sse4.1"))) inline float round_up(float x)
{
    return _mm_cvtss_f32(_mm_round_ss(_mm_set_ss(x), _mm_set_ss(x), _MM_FROUND_TO_POS_INF));
}

// maxss: a if a > b, else b (so b when either is NaN).
inline float max_of(float a, float b) { return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(a), _mm_set_ss(b))); }

// minss: a if a < b, else b (so b when either is NaN).
inline float min_of(float a, float b) { return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(a), _mm_set_ss(b))); }

// cvttss2si: truncates toward zero; out of range or NaN gives INT32_MIN (a C++ cast would be
// undefined there).
inline int32_t truncate(float x) { return _mm_cvttss_si32(_mm_set_ss(x)); }

// divss: a / b, with b read from memory as the original does (no folding of a known divisor).
inline float divide(float a, const float &b) { return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_load_ss(&b))); }

// vcvtsi2ss on an unsigned 64-bit value, as compilers emit it: values with the top bit set are
// halved (keeping the low bit, so the rounding is the same), converted, and doubled.
inline float u64_to_float(uint64_t x)
{
    if (static_cast<int64_t>(x) >= 0) return _mm_cvtss_f32(_mm_cvtsi64_ss(_mm_setzero_ps(), static_cast<int64_t>(x)));
    const uint64_t half = (x >> 1) | (x & 1);
    const float f = _mm_cvtss_f32(_mm_cvtsi64_ss(_mm_setzero_ps(), static_cast<int64_t>(half)));
    return f + f;
}

// vcvttsd2si to an unsigned 64-bit value: at 2^63 and above, subtract 2^63 and set the top bit.
inline uint64_t double_to_u64(double d)
{
    constexpr double two_pow_63 = 9223372036854775808.0;
    constexpr uint64_t top_bit = 0x8000000000000000ull;
    if (d >= two_pow_63) return static_cast<uint64_t>(_mm_cvttsd_si64(_mm_set_sd(d - two_pow_63))) ^ top_bit;
    return static_cast<uint64_t>(_mm_cvttsd_si64(_mm_set_sd(d)));
}

}  // namespace engine
