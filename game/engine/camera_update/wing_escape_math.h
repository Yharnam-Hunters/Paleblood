// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera wing escape. Include through math.h.
#pragma once
namespace engine::camera_wing_math
{
inline constexpr int32_t pi_bits = 0x40490fdb;
inline constexpr int32_t float_absolute_mask = 0x7fffffff;
inline constexpr int lane_broadcast_stride = 0x55;

inline Vec4 add(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("%{vex3%} vaddps %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 subtract(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("%{vex3%} vsubps %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 multiply(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("%{vex3%} vmulps %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 divide(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("%{vex3%} vdivps %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 not_equal(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("vcmpneqps %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 less_low(Vec4 left, Vec4 right)
{
    Vec4 result;
    asm volatile("%{vex3%} vcmpltss %2, %1, %0" : "=x"(result.v) : "x"(left.v), "x"(right.v));
    return result;
}

inline Vec4 bit_and(Vec4 left, Vec4 right)
{
    return Vec4(_mm_and_ps(left.v, right.v));
}
inline Vec4 bit_and_not(Vec4 left, Vec4 right)
{
    return Vec4(_mm_andnot_ps(left.v, right.v));
}
inline Vec4 bit_or(Vec4 left, Vec4 right)
{
    return Vec4(_mm_or_ps(left.v, right.v));
}
inline Vec4 scalar_vector(float value)
{
    return Vec4(_mm_set_ss(value));
}

template <int Lane> inline Vec4 broadcast(Vec4 value)
{
    static_assert(Lane >= 0 && Lane < Vec4::lanes);
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Lane * lane_broadcast_stride)));
}

inline float capture(const float &source)
{
    float value;
    asm volatile("vmovss %1, %0" : "=x"(value) : "m"(source) : "memory");
    return value;
}

inline Vec4 capture(const Vec4 &source)
{
    Vec4 value;
    asm volatile("vmovaps %1, %0" : "=x"(value.v) : "m"(source.v) : "memory");
    return value;
}

inline void publish(Vec4 &target, Vec4 value)
{
    asm volatile("vmovaps %1, %0" : "=m"(target.v) : "x"(value.v) : "memory");
}

} // namespace engine::camera_wing_math

namespace engine::camera_wing_math
{

inline float add(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vaddss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
inline float subtract(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vsubss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
inline float multiply(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vmulss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
inline float divide(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vdivss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
inline float minimum(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vminss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
inline bool ordered_greater(float left, float right)
{
    uint8_t result;
    asm volatile("vucomiss %2, %1; seta %0" : "=qm"(result) : "x"(left), "x"(right) : "cc");
    return result != 0;
}
inline bool ordered_zero(float value)
{
    uint8_t equal, ordered;
    const float zero = 0.0f;
    asm volatile("vucomiss %3, %2; sete %0; setnp %1"
                 : "=qm"(equal), "=qm"(ordered)
                 : "x"(value), "x"(zero)
                 : "cc");
    return equal != 0 && ordered != 0;
}
inline uint8_t capture(const uint8_t &source)
{
    uint8_t value;
    asm volatile("movb %1, %0" : "=q"(value) : "m"(source) : "memory");
    return value;
}
inline void publish(float &target, float value)
{
    asm volatile("vmovss %1, %0" : "=m"(target) : "x"(value) : "memory");
}
inline void publish(uint8_t &target, uint8_t value)
{
    asm volatile("movb %1, %0" : "=m"(target) : "q"(value) : "memory");
}

inline Vec4 lanes(float x, float y, float z, float w)
{
    return Vec4(_mm_set_ps(w, z, y, x));
}
inline Vec4 horizontal_add(Vec4 value)
{
    Vec4 result;
    asm volatile("%{vex3%} vhaddps %1, %1, %0" : "=x"(result.v) : "x"(value.v));
    return result;
}
inline Vec4 sum_four(Vec4 value)
{
    return horizontal_add(horizontal_add(value));
}
inline float absolute_bits(float value)
{
    return _mm_cvtss_f32(
        _mm_and_ps(_mm_set_ss(value), _mm_castsi128_ps(_mm_set1_epi32(float_absolute_mask))));
}
inline float xor_low(float value, Vec4 mask)
{
    return _mm_cvtss_f32(_mm_xor_ps(_mm_set_ss(value), mask.v));
}
inline float signed_pi(float angle)
{
    const __m128i sign = _mm_and_si128(_mm_castps_si128(_mm_set_ss(angle)), _mm_set1_epi32(0x80000000));
    return _mm_cvtss_f32(_mm_castsi128_ps(_mm_or_si128(sign, _mm_set1_epi32(pi_bits))));
}
inline float truncate_32_to_float(float value)
{
    return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), _mm_cvttss_si32(_mm_set_ss(value))));
}
inline Vec4 scalar_broadcast(float value)
{
    return Vec4(_mm_set1_ps(value));
}
inline Vec4 squared_xyz_length(Vec4 value)
{
    const Vec4 squares = multiply(value, value);
    return add(broadcast<Vec4::lane_z>(squares), add(broadcast<0>(squares), broadcast<1>(squares)));
}

} // namespace engine::camera_wing_math
