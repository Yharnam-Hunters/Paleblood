// SPDX-License-Identifier: GPL-2.0-or-later
// Sine and cosine of four lanes by octants (first replaced in camera_axis_angle_quaternion,
// 0x00848e80): the magnitude is reduced by the nearest even multiple of pi/4 (pi/4 split in
// three parts), two minimax polynomials are chosen and negated by octant, and both results are
// clamped to at most 1. The clamp is a minps with 1 as its second operand, so a NaN lane (a NaN
// or infinite input) comes out as 1 before its sign is applied. Each step is the instruction the
// original uses, in its operand order, so results match bit for bit.
#pragma once

#include <immintrin.h>

#include <cstdint>

#include "rotation_math.h"
#include "vector.h"

namespace engine::octant {

namespace detail {

constexpr uint32_t sign_bit = 0x80000000u;
constexpr int32_t integer_one = 1, integer_two = 2, integer_four = 4;
// Octant bit 2 moved to the float sign bit.
constexpr int octant_sign_shift = 29;
constexpr float one_half = 0.5f;
// 4/pi, and -pi/4 split in three parts for the argument reduction.
constexpr uint32_t four_over_pi_bits = 0x3fa2f983u;
constexpr uint32_t minus_quarter_pi_1_bits = 0xbf490000u;
constexpr uint32_t minus_quarter_pi_2_bits = 0xb97da000u;
constexpr uint32_t minus_quarter_pi_3_bits = 0xb3222169u;
// Cosine polynomial (in z = r^2, times z^2, then 1 - z/2 added) and sine polynomial (in z,
// times z r, then r added).
constexpr uint32_t cosine_c0_bits = 0x37ccf5ceu, cosine_c1_bits = 0xbab6061au, cosine_c2_bits = 0x3d2aaaa5u;
constexpr uint32_t sine_s0_bits = 0xb94ca1f9u, sine_s1_bits = 0x3c08839eu, sine_s2_bits = 0xbe2aaaa3u;

inline __m128 splat_bits(uint32_t bits) { return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int32_t>(bits))); }
inline __m128i splat_int(int32_t value) { return _mm_set1_epi32(value); }

}  // namespace detail

// Sine and cosine of each lane of angles.
__attribute__((target("sse4.1"))) inline rotation::SineCosine sine_cosine(Vec4 angles)
{
    using namespace detail;
    using rotation::detail::add;
    using rotation::detail::mul;
    const __m128 x = angles.v;
    const __m128 sign = splat_bits(sign_bit);
    const __m128 magnitude = _mm_andnot_ps(sign, x);
    __m128 sine_sign = _mm_and_ps(sign, x);

    // The octant, rounded up to even: (int(|x| * 4/pi) + 1) & ~1.
    const __m128i one = splat_int(integer_one), two = splat_int(integer_two), four = splat_int(integer_four);
    __m128i octant = _mm_cvttps_epi32(mul(magnitude, splat_bits(four_over_pi_bits)));
    octant = _mm_andnot_si128(one, _mm_add_epi32(octant, one));
    const __m128 whole = _mm_cvtepi32_ps(octant);
    // Cosine is negated in octants 2-5 (bit 2 of octant - 2 clear); sine in octants 4-7.
    const __m128 cosine_sign =
        _mm_castsi128_ps(_mm_slli_epi32(_mm_andnot_si128(_mm_sub_epi32(octant, two), four), octant_sign_shift));
    sine_sign = _mm_xor_ps(sine_sign, _mm_castsi128_ps(_mm_slli_epi32(_mm_and_si128(octant, four), octant_sign_shift)));

    // r = |x| - whole * pi/4, in three steps (all three products first).
    const __m128 part_1 = mul(whole, splat_bits(minus_quarter_pi_1_bits));
    const __m128 part_2 = mul(whole, splat_bits(minus_quarter_pi_2_bits));
    const __m128 part_3 = mul(whole, splat_bits(minus_quarter_pi_3_bits));
    const __m128 r = add(add(add(magnitude, part_1), part_2), part_3);
    const __m128 z = mul(r, r);

    __m128 cosine_poly = add(mul(z, splat_bits(cosine_c0_bits)), splat_bits(cosine_c1_bits));
    cosine_poly = add(mul(z, cosine_poly), splat_bits(cosine_c2_bits));
    cosine_poly = mul(mul(z, z), cosine_poly);
    cosine_poly = _mm_sub_ps(cosine_poly, mul(z, _mm_set1_ps(one_half)));
    const __m128 ones = _mm_set1_ps(1.0f);
    cosine_poly = add(ones, cosine_poly);

    __m128 sine_poly = add(mul(z, splat_bits(sine_s0_bits)), splat_bits(sine_s1_bits));
    sine_poly = add(mul(z, sine_poly), splat_bits(sine_s2_bits));
    sine_poly = mul(r, mul(z, sine_poly));
    sine_poly = add(r, sine_poly);

    // Octants 0 and 4 take the sine polynomial for the sine, 2 and 6 the cosine polynomial.
    // The cosine is formed as (cos - masked cos) + (sin - masked sin), as in the original:
    // the lane it drops is a self-subtraction, NaN for an infinite or NaN polynomial.
    const __m128 sine_from_sine_poly = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(octant, two), _mm_setzero_si128()));
    const __m128 sine_part = _mm_sub_ps(sine_poly, _mm_and_ps(sine_from_sine_poly, sine_poly));
    const __m128 cosine_part = _mm_sub_ps(cosine_poly, _mm_andnot_ps(sine_from_sine_poly, cosine_poly));
    __m128 sine = _mm_blendv_ps(cosine_poly, sine_poly, sine_from_sine_poly);
    __m128 cosine = add(cosine_part, sine_part);

    // minps keeps the second operand (1) when the first is NaN.
    sine = _mm_min_ps(sine, ones);
    cosine = _mm_min_ps(cosine, ones);
    return {Vec4(_mm_xor_ps(sine, sine_sign)), Vec4(_mm_xor_ps(cosine, cosine_sign))};
}

}  // namespace engine::octant
