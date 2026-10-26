// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera subject. Include through math.h.
#pragma once
namespace engine::camera_subject_math
{
inline constexpr int lane_broadcast_stride = 0x55;

// Pin the VEX source order: both payload selection and the literal zero products matter.
// Explicit VEX3 prevents assembler shortening from commuting a high-register right operand.
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

inline float multiply_low(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vmulss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline bool is_ordered_zero(float value)
{
    uint8_t equal, ordered;
    const float zero = 0.0f;
    asm volatile("vucomiss %3, %2; sete %0; setnp %1"
                 : "=qm"(equal), "=qm"(ordered)
                 : "x"(value), "x"(zero)
                 : "cc");
    return equal != 0 && ordered != 0;
}

template <int Lane> inline Vec4 broadcast(Vec4 value)
{
    static_assert(Lane >= 0 && Lane < Vec4::lanes);
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Lane * lane_broadcast_stride)));
}

inline Vec4 scalar_vector(float value)
{
    return Vec4(_mm_set_ss(value));
}
inline Vec4 bit_and(Vec4 left, Vec4 right)
{
    return Vec4(_mm_and_ps(left.v, right.v));
}
inline Vec4 bit_or(Vec4 left, Vec4 right)
{
    return Vec4(_mm_or_ps(left.v, right.v));
}

inline Vec4 sine_cosine_vector(float sine, float cosine)
{
    const Vec4 sine_low = scalar_vector(sine);
    const Vec4 cosine_low = scalar_vector(cosine);
    constexpr int sine_zero_cosine_zero = 0xcc;
    return Vec4(_mm_shuffle_ps(sine_low.v, cosine_low.v, sine_zero_cosine_zero));
}

inline Vec4 swap_x_z(Vec4 value)
{
    constexpr int z_y_x_w = 0xc6;
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), z_y_x_w)));
}

// Pin guest field capture/publication order as well as the arithmetic. The memory clobber
// prevents the compiler from moving another guest field access across these boundaries.
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

} // namespace engine::camera_subject_math
