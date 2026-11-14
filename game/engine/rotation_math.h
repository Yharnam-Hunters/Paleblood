// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's inline vector math for rotations, as the compiler expanded it into its callers
// (first replaced in render_update_model_wind, 0x02713870): a four-lane sine/cosine polynomial,
// a quaternion from XYZ Euler half-angle sines and cosines, the rotation matrix of a
// quaternion, and a Newton-refined vector length. Each step is the instruction the original
// uses, in its operand order, so results match bit for bit (NaN payloads included).
#pragma once

#include <immintrin.h>

#include <cstdint>

#include "vector.h"

namespace engine::rotation {

// Sine and cosine of each lane, as one polynomial pass.
struct SineCosine {
    Vec4 sine;
    Vec4 cosine;
};

// The three columns of a rotation matrix.
struct RotationColumns {
    Vec4 x;
    Vec4 y;
    Vec4 z;
};

namespace detail {

constexpr uint32_t sign_bit = 0x80000000u;
constexpr uint32_t magnitude_bits = 0x7fffffffu;
constexpr uint32_t infinity_bits = 0x7f800000u;
constexpr int32_t quadrant_mask = 3;
constexpr int32_t odd_quadrant_bit = 1;
constexpr int32_t negate_quadrant_bit = 2;
constexpr float one_half = 0.5f;
constexpr float three = 3.0f;
// 2/pi, and pi/2 split in two parts for the argument reduction.
constexpr uint32_t two_over_pi_bits = 0x3f22f983u;
constexpr uint32_t half_pi_high_bits = 0x3fc90fdau;
constexpr uint32_t half_pi_low_bits = 0x33a22168u;
// Cosine polynomial (in r^2) and sine polynomial (in r^2, times r^3).
constexpr uint32_t cosine_c3_bits = 0xbab24993u, cosine_c2_bits = 0x3d2aa036u, cosine_c1_bits = 0xbeffffdfu;
constexpr uint32_t sine_s3_bits = 0xb94c8c6eu, sine_s2_bits = 0x3c088342u, sine_s1_bits = 0xbe2aaaa1u;

// addps / mulps in the original's operand order (the first operand's NaN payload wins).
inline __m128 add(__m128 first, __m128 second)
{
    asm("addps %1, %0" : "+x"(first) : "x"(second));
    return first;
}
inline __m128 mul(__m128 first, __m128 second)
{
    asm("mulps %1, %0" : "+x"(first) : "x"(second));
    return first;
}

inline __m128 splat_bits(uint32_t bits) { return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int32_t>(bits))); }
inline __m128i lanes_zero_where_clear(__m128i value, int32_t bit)
{
    return _mm_cmpeq_epi32(_mm_and_si128(value, _mm_set1_epi32(bit)), _mm_setzero_si128());
}

// Lane selections of the quaternion and matrix code (shufps / pshufd immediates).
constexpr int pick_yxxw = 0xc1, pick_zzyw = 0xda, pick_xxyw = 0xd0, pick_zyzw = 0xe6;
constexpr int pick_wwww = 0xff, pick_yzxw = 0xc9, pick_sum_diff = 0x49, pick_xzwy = 0x78;
constexpr int pick_xzxx = 0x08, pick_diag_x_sums = 0x4c, pick_diag_y_sums = 0xed, pick_zxwy = 0x72;
constexpr int pick_xy_of_first_zw_of_second = 0xe4;
constexpr int all_x = 0x00, all_y = 0x55, all_z = 0xaa, all_w = 0xff;

}  // namespace detail

// Sine and cosine of the four lanes: reduction by multiples of pi/2, two minimax polynomials
// chosen and negated by quadrant; a zero lane keeps its own value (and sign) as the sine, and
// an infinite lane gives all-ones bits (a NaN) in both results.
__attribute__((target("sse4.1"))) inline SineCosine sine_cosine(Vec4 angles)
{
    using namespace detail;
    const __m128 x = angles.v;
    const __m128 scaled = mul(x, splat_bits(two_over_pi_bits));
    const __m128 rounding = _mm_or_ps(_mm_and_ps(x, splat_bits(sign_bit)), _mm_set1_ps(one_half));
    const __m128i quadrant = _mm_cvttps_epi32(add(scaled, rounding));
    const __m128i next_quadrant = _mm_add_epi32(_mm_and_si128(quadrant, _mm_set1_epi32(quadrant_mask)),
                                                _mm_set1_epi32(odd_quadrant_bit));
    const __m128 whole = _mm_cvtepi32_ps(quadrant);
    const __m128 partly_reduced = _mm_sub_ps(x, mul(whole, splat_bits(half_pi_high_bits)));
    const __m128 r = _mm_sub_ps(partly_reduced, mul(whole, splat_bits(half_pi_low_bits)));
    const __m128 r2 = mul(r, r);

    __m128 cosine = add(mul(r2, splat_bits(cosine_c3_bits)), splat_bits(cosine_c2_bits));
    __m128 sine = add(mul(r2, splat_bits(sine_s3_bits)), splat_bits(sine_s2_bits));
    cosine = add(mul(r2, cosine), splat_bits(cosine_c1_bits));
    sine = mul(r2, sine);
    cosine = mul(r2, cosine);
    const __m128 r3 = mul(r, r2);
    sine = add(sine, splat_bits(sine_s1_bits));
    cosine = add(cosine, _mm_set1_ps(1.0f));
    sine = add(r, mul(r3, sine));

    // Odd quadrants swap the polynomials; quadrants 2 and 3 negate.
    const __m128 sine_even = _mm_castsi128_ps(lanes_zero_where_clear(quadrant, odd_quadrant_bit));
    const __m128 cosine_even = _mm_castsi128_ps(lanes_zero_where_clear(next_quadrant, odd_quadrant_bit));
    __m128 sine_out = _mm_blendv_ps(cosine, sine, sine_even);
    __m128 cosine_out = _mm_blendv_ps(cosine, sine, cosine_even);
    const __m128 sign = splat_bits(sign_bit);
    sine_out = _mm_blendv_ps(_mm_xor_ps(sine_out, sign), sine_out,
                             _mm_castsi128_ps(lanes_zero_where_clear(quadrant, negate_quadrant_bit)));
    cosine_out = _mm_blendv_ps(_mm_xor_ps(cosine_out, sign), cosine_out,
                               _mm_castsi128_ps(lanes_zero_where_clear(next_quadrant, negate_quadrant_bit)));

    sine_out = _mm_blendv_ps(sine_out, x, _mm_cmpeq_ps(x, _mm_setzero_ps()));
    const __m128 infinite = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(_mm_and_ps(x, splat_bits(magnitude_bits))),
                                                             _mm_set1_epi32(static_cast<int32_t>(infinity_bits))));
    return {Vec4(_mm_or_ps(sine_out, infinite)), Vec4(_mm_or_ps(cosine_out, infinite))};
}

// The quaternion (x, y, z, w) of an X, then Y, then Z rotation, from the sines and cosines of
// the half angles: (sx cy cz, cx sy cz, cx cy sz, cx cy cz) plus (cx sy sz, -sx cy sz,
// -sx sy cz, sx sy sz), each product in the original's order.
__attribute__((target("sse4.1"))) inline Vec4 quaternion_from_half_angles(SineCosine half)
{
    using namespace detail;
    constexpr int lane_x_only = 0b0001, lane_y_only = 0b0010, lane_z_only = 0b0100;
    const __m128 s = half.sine.v, c = half.cosine.v;
    const __m128 sx = _mm_shuffle_ps(s, s, all_x), sy = _mm_shuffle_ps(s, s, all_y), sz = _mm_shuffle_ps(s, s, all_z);
    const __m128 cx = _mm_shuffle_ps(c, c, all_x), cy = _mm_shuffle_ps(c, c, all_y), cz = _mm_shuffle_ps(c, c, all_z);
    const __m128 first_x = _mm_blend_ps(cx, sx, lane_x_only);    // (sx, cx, cx, cx)
    const __m128 first_y = _mm_blend_ps(cy, sy, lane_y_only);    // (cy, sy, cy, cy)
    const __m128 first_z = _mm_blend_ps(cz, sz, lane_z_only);    // (cz, cz, sz, cz)
    const __m128 second_x = _mm_blend_ps(sx, cx, lane_x_only);   // (cx, sx, sx, sx)
    const __m128 second_y = _mm_blend_ps(sy, cy, lane_y_only);   // (sy, cy, sy, sy)
    const __m128 second_z = _mm_blend_ps(sz, cz, lane_z_only);   // (sz, sz, cz, sz)
    const __m128 signs = _mm_set_ps(1.0f, -1.0f, -1.0f, 1.0f);
    const __m128 first = mul(mul(first_x, first_y), first_z);
    const __m128 second = mul(mul(second_y, mul(second_x, signs)), second_z);
    return Vec4(add(first, second));
}

// The rotation matrix of a unit quaternion, as three columns with w = 0.
inline RotationColumns rotation_from_quaternion(Vec4 quaternion)
{
    using namespace detail;
    const __m128i xyz_mask = _mm_set_epi32(0, -1, -1, -1);
    const __m128 q = quaternion.v;
    const __m128 doubled = add(q, q);
    const __m128i squares = _mm_castps_si128(mul(q, doubled));
    const __m128 yy_xx_xx = _mm_castsi128_ps(_mm_and_si128(_mm_shuffle_epi32(squares, pick_yxxw), xyz_mask));
    const __m128 zz_zz_yy = _mm_castsi128_ps(_mm_and_si128(_mm_shuffle_epi32(squares, pick_zzyw), xyz_mask));
    const __m128 diagonal = _mm_sub_ps(_mm_sub_ps(_mm_set_ps(0.0f, 1.0f, 1.0f, 1.0f), yy_xx_xx), zz_zz_yy);

    const __m128i qi = _mm_castps_si128(q), di = _mm_castps_si128(doubled);
    const __m128 cross = mul(_mm_castsi128_ps(_mm_shuffle_epi32(qi, pick_xxyw)),
                                    _mm_castsi128_ps(_mm_shuffle_epi32(di, pick_zyzw)));
    const __m128 with_w = mul(_mm_castsi128_ps(_mm_shuffle_epi32(qi, pick_wwww)),
                                     _mm_castsi128_ps(_mm_shuffle_epi32(di, pick_yzxw)));
    const __m128 sums = add(cross, with_w);
    const __m128 differences = _mm_sub_ps(cross, with_w);

    const __m128 mixed = _mm_castsi128_ps(
        _mm_shuffle_epi32(_mm_castps_si128(_mm_shuffle_ps(sums, differences, pick_sum_diff)), pick_xzwy));
    const __m128 first_pair = _mm_castsi128_ps(
        _mm_shuffle_epi32(_mm_castps_si128(_mm_move_ss(differences, sums)), pick_xzxx));
    const __m128 column_x = _mm_castsi128_ps(
        _mm_shuffle_epi32(_mm_castps_si128(_mm_shuffle_ps(diagonal, mixed, pick_diag_x_sums)), pick_xzwy));
    const __m128 column_y = _mm_castsi128_ps(
        _mm_shuffle_epi32(_mm_castps_si128(_mm_shuffle_ps(diagonal, mixed, pick_diag_y_sums)), pick_zxwy));
    const __m128 column_z = _mm_shuffle_ps(first_pair, diagonal, pick_xy_of_first_zw_of_second);
    return {Vec4(column_x), Vec4(column_y), Vec4(column_z)};
}

// The rotation applied to a vector whose w lane rides along: (w (0,0,0,1) + z Z) + (x X + y Y).
inline Vec4 rotate(const RotationColumns &rotation, Vec4 vector)
{
    using namespace detail;
    const __m128i v = _mm_castps_si128(vector.v);
    const __m128 x = _mm_castsi128_ps(_mm_shuffle_epi32(v, all_x));
    const __m128 y = _mm_castsi128_ps(_mm_shuffle_epi32(v, all_y));
    const __m128 z = _mm_castsi128_ps(_mm_shuffle_epi32(v, all_z));
    const __m128 w = _mm_castsi128_ps(_mm_shuffle_epi32(v, all_w));
    const __m128 xy = add(mul(x, rotation.x.v), mul(y, rotation.y.v));
    const __m128 zw = add(mul(w, _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f)), mul(z, rotation.z.v));
    return Vec4(add(zw, xy));
}

// x*x + y*y + z*z of a vector, summed as z^2 + (x^2 + y^2).
inline float squared_length(Vec4 vector)
{
    using namespace detail;
    const __m128 squares = mul(vector.v, vector.v);
    const __m128 x = _mm_shuffle_ps(squares, squares, detail::all_x);
    const __m128 y = _mm_shuffle_ps(squares, squares, detail::all_y);
    const __m128 z = _mm_shuffle_ps(squares, squares, detail::all_z);
    return _mm_cvtss_f32(add(z, add(x, y)));
}

// sqrt(squared) from the hardware reciprocal square root and two Newton steps; a zero input or
// a zero estimate (infinity) gives the input itself. The estimate (rsqrtps) is the CPU's own, as
// in the original. The original's two inlined copies differ only in the order of one product's
// operands (squared * estimate, or estimate * squared), which cannot change the result: when the
// input is a NaN the estimate is that same NaN.
__attribute__((target("sse4.1"))) inline float refined_length(float squared)
{
    using namespace detail;
    const __m128 s = _mm_set1_ps(squared);
    const __m128 estimate = _mm_rsqrt_ps(s);
    const __m128 zero = _mm_setzero_ps();
    const __m128 use_input = _mm_or_ps(_mm_cmpeq_ps(s, zero), _mm_cmpeq_ps(estimate, zero));
    const __m128 half = _mm_set1_ps(one_half), threes = _mm_set1_ps(three);
    __m128 r = mul(mul(estimate, half), _mm_sub_ps(threes, mul(estimate, mul(s, estimate))));
    r = mul(mul(r, half), _mm_sub_ps(threes, mul(r, mul(s, r))));
    return _mm_cvtss_f32(_mm_blendv_ps(mul(s, r), s, use_input));
}

}  // namespace engine::rotation
