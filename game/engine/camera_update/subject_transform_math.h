// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera subject transform. Include through math.h.
#pragma once
namespace engine::camera_subject_transform_math
{

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

inline Vec4 bit_and(Vec4 left, Vec4 right)
{
    return Vec4(_mm_and_ps(left.v, right.v));
}

inline Vec4 capture(const Vec4 &source)
{
    Vec4 value;
    asm volatile("vmovaps %1, %0" : "=x"(value.v) : "m"(source.v) : "memory");
    return value;
}

template <typename T> inline T *capture_pointer(T *const &source)
{
    T *value;
    asm volatile("movq %1, %0" : "=r"(value) : "m"(source) : "memory");
    return value;
}

inline uint8_t capture_byte(const uint8_t &source)
{
    uint8_t value;
    asm volatile("movb %1, %0" : "=q"(value) : "m"(source) : "memory");
    return value;
}

inline void publish(Vec4 &target, Vec4 value)
{
    asm volatile("vmovaps %1, %0" : "=m"(target.v) : "x"(value.v) : "memory");
}

// Original scalar loads/inserts/shuffles discard all four generated W lanes. This bitwise
// blend is their proven lane mapping; it performs no float arithmetic on the retained XYZ.
__attribute__((target("sse4.1"))) inline Vec4 xyz_with_w(Vec4 value, Vec4 w_source)
{
    constexpr int w_lane = 0b1000;
    return Vec4(_mm_blend_ps(value.v, w_source.v, w_lane));
}

__attribute__((target("sse4.1"))) inline Vec4 xyz_with_source_z_in_w(Vec4 value, Vec4 source)
{
    constexpr int source_z_to_destination_w = 0xb0;
    return Vec4(_mm_insert_ps(value.v, source.v, source_z_to_destination_w));
}

} // namespace engine::camera_subject_transform_math
