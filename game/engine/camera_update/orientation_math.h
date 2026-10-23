// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera orientation. Include through math.h.
#pragma once
namespace engine::camera_orientation_math
{
inline constexpr int all_lanes_mask = 0xf;
using namespace engine::camera_wing_math;

template <int Order> inline Vec4 shuffle_words(Vec4 value)
{
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Order)));
}

inline bool contains_nan(Vec4 value, Vec4 exponent_mask)
{
    const __m128i words = _mm_castps_si128(value.v);
    const __m128i exponents = _mm_castps_si128(exponent_mask.v);
    const __m128i absolute_mask = _mm_set1_epi32(0x7fffffff);
    const __m128i exponent_equal = _mm_cmpeq_epi32(_mm_and_si128(words, exponents), exponents);
    const __m128i infinity_equal = _mm_cmpeq_epi32(_mm_and_si128(words, absolute_mask), exponents);
    const Vec4 nan_words(_mm_castsi128_ps(_mm_andnot_si128(infinity_equal, exponent_equal)));
    return _mm_movemask_ps(engine::camera_root::equal(nan_words, Vec4::zero()).v) != all_lanes_mask;
}

} // namespace engine::camera_orientation_math
