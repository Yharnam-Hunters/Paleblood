// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera locked geometry. Include through math.h.
#pragma once
namespace engine::camera_locked_geometry_math
{
inline constexpr int32_t pi_bits = 0x40490fdb;
namespace packed = camera_subject_math;
namespace scalar = camera_control_math;
namespace arithmetic = camera_target_math;

inline float minimum(float first, float second)
{
    float result;
    // MINSS returns source2 for unordered or equal inputs, including signed zeros.
    asm volatile("%{vex3%} vminss %2, %1, %0" : "=x"(result) : "x"(first), "x"(second));
    return result;
}

inline float absolute(float value)
{
    const __m128i bits = _mm_and_si128(_mm_castps_si128(_mm_set_ss(value)), _mm_set1_epi32(0x7fffffff));
    return _mm_cvtss_f32(_mm_castsi128_ps(bits));
}

inline float signed_pi(float value)
{
    const __m128i sign =
        _mm_and_si128(_mm_castps_si128(_mm_set_ss(value)), _mm_set1_epi32(int32_t(0x80000000u)));
    return _mm_cvtss_f32(_mm_castsi128_ps(_mm_or_si128(sign, _mm_set1_epi32(pi_bits))));
}

inline float truncate_32_to_float(float value)
{
    // Retain x86 indefinite integer results for NaN/out-of-range: no C++ float-to-int cast.
    const int32_t truncated = _mm_cvttss_si32(_mm_set_ss(value));
    return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), truncated));
}

inline Vec4 lanes(float x, float y, float z, float w)
{
    return Vec4(_mm_set_ps(w, z, y, x));
}

inline Vec4 horizontal_add_self(Vec4 value)
{
    Vec4 result;
    asm volatile("%{vex3%} vhaddps %1, %1, %0" : "=x"(result.v) : "x"(value.v));
    return result;
}

inline float sum_four(Vec4 value)
{
    return horizontal_add_self(horizontal_add_self(value)).x();
}

__attribute__((target("sse4.1"))) inline Vec4 signed_pi_pairs(Vec4 movement, Vec4 sign_mask, Vec4 pi_bits)
{
    constexpr int low_words = 0x08;
    const __m128i widened = _mm_cvtepu32_epi64(_mm_castps_si128(movement.v));
    const __m128i signed_pi_bits =
        _mm_or_si128(_mm_and_si128(widened, _mm_castps_si128(sign_mask.v)), _mm_castps_si128(pi_bits.v));
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(signed_pi_bits, low_words)));
}
inline Vec4 signed64_pair_low_words(float pitch_quotient, float yaw_quotient)
{
    constexpr int low_words = 0x08;
    const int64_t pitch_integral = _mm_cvttss_si64(_mm_set_ss(pitch_quotient));
    const int64_t yaw_integral = _mm_cvttss_si64(_mm_set_ss(yaw_quotient));
    const __m128i joined =
        _mm_unpacklo_epi64(_mm_cvtsi64_si128(pitch_integral), _mm_cvtsi64_si128(yaw_integral));
    const __m128i words = _mm_shuffle_epi32(joined, low_words);
    return Vec4(_mm_cvtepi32_ps(words));
}

} // namespace engine::camera_locked_geometry_math
