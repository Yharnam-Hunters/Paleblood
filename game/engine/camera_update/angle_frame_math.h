// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera angle frame. Include through math.h.
#pragma once
namespace engine::camera_angle_frame_math
{
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

} // namespace engine::camera_angle_frame_math
