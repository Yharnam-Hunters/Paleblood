// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera candidate collision. Include through math.h.
#pragma once
namespace engine::camera_candidate_collision_math
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

inline float maximum(float left, float right)
{
    // MAXSS retains source2 for equality and unordered values, including signed zero.
    float result;
    asm volatile("%{vex3%} vmaxss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline bool ordered_greater(float left, float right)
{
    uint8_t result;
    asm volatile("vucomiss %2, %1; seta %0" : "=qm"(result) : "x"(left), "x"(right) : "cc");
    return result != 0;
}

inline uint8_t capture(const uint8_t &source)
{
    uint8_t result;
    asm volatile("movb %1, %0" : "=q"(result) : "m"(source) : "memory");
    return result;
}

inline float capture(const float &source)
{
    float result;
    asm volatile("vmovss %1, %0" : "=x"(result) : "m"(source) : "memory");
    return result;
}

inline Vec4 capture(const Vec4 &source)
{
    Vec4 result;
    asm volatile("vmovaps %1, %0" : "=x"(result.v) : "m"(source.v) : "memory");
    return result;
}

inline void publish(Vec4 &target, Vec4 value)
{
    asm volatile("vmovaps %1, %0" : "=m"(target.v) : "x"(value.v) : "memory");
}

template <int Lane> inline Vec4 broadcast(Vec4 value)
{
    static_assert(Lane >= 0 && Lane < Vec4::lanes);
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Lane * lane_broadcast_stride)));
}

inline Vec4 broadcast(float value)
{
    return Vec4(_mm_set1_ps(value));
}

inline Vec4 squared_xyz_length(Vec4 value)
{
    const Vec4 squares = multiply(value, value);
    const Vec4 x = broadcast<0>(squares);
    const Vec4 y = broadcast<1>(squares);
    const Vec4 z = broadcast<Vec4::lane_z>(squares);
    return add(z, add(x, y));
}

} // namespace engine::camera_candidate_collision_math
