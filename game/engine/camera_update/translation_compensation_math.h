// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera translation compensation. Include through math.h.
#pragma once
namespace engine::camera_translation_compensation_math
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
    // MINSS selects source 2 for unordered or equal inputs, including signed-zero ties.
    float result;
    asm volatile("%{vex3%} vminss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline bool ordered_greater(float left, float right)
{
    unsigned char result;
    asm volatile("vucomiss %2, %1; seta %0" : "=qm"(result) : "x"(left), "x"(right) : "cc");
    return result != 0;
}

inline float absolute_bits(float value)
{
    const __m128 mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
    return _mm_cvtss_f32(_mm_and_ps(_mm_set_ss(value), mask));
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

inline void publish(float &target, float value)
{
    asm volatile("vmovss %1, %0" : "=m"(target) : "x"(value) : "memory");
}

inline void publish_bits(float &target, uint32_t value)
{
    asm volatile("movl %1, %0" : "=m"(target) : "ir"(value) : "memory");
}

inline void publish(Vec4 &target, Vec4 value)
{
    asm volatile("vmovaps %1, %0" : "=m"(target.v) : "x"(value.v) : "memory");
}

inline Vec4 squared_xyz_length(Vec4 difference)
{
    const Vec4 squares = multiply(difference, difference);
    const Vec4 x = broadcast<0>(squares);
    const Vec4 y = broadcast<1>(squares);
    const Vec4 z = broadcast<Vec4::lane_z>(squares);
    return add(z, add(x, y));
}

} // namespace engine::camera_translation_compensation_math
