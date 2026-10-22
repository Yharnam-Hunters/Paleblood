// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera free input. Include through math.h.
#pragma once
namespace engine::camera_free_input_math
{
namespace packed = camera_subject_math;

inline Vec4 signed_greater_bits(Vec4 first, Vec4 second)
{
    Vec4 result;
    asm volatile("%{vex3%} vpcmpgtd %2, %1, %0" : "=x"(result.v) : "x"(first.v), "x"(second.v));
    return result;
}

inline Vec4 rounded_integer_bits(Vec4 value)
{
    Vec4 result;
    // CVTPS2DQ uses current MXCSR rounding. This is deliberately not truncation/roundf.
    asm volatile("vcvtps2dq %1, %0" : "=x"(result.v) : "x"(value.v));
    return result;
}

inline Vec4 integer_bits_to_float(Vec4 value)
{
    Vec4 result;
    asm volatile("vcvtdq2ps %1, %0" : "=x"(result.v) : "x"(value.v));
    return result;
}

inline Vec4 bit_xor(Vec4 first, Vec4 second)
{
    return Vec4(_mm_xor_ps(first.v, second.v));
}
inline Vec4 bit_and_not(Vec4 first, Vec4 second)
{
    return Vec4(_mm_andnot_ps(first.v, second.v));
}

} // namespace engine::camera_free_input_math
