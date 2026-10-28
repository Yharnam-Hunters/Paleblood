// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera parameter. Include through math.h.
#pragma once
namespace engine::camera_parameter_math
{

// Keep each scalar instruction's operand order, including competing NaN payloads.
// Operand order follows the camera parameter interpolation instructions.
inline float add(float left, float right)
{
    float result;
    asm volatile("vaddss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline float subtract(float left, float right)
{
    float result;
    asm volatile("vsubss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline float multiply(float left, float right)
{
    float result;
    asm volatile("vmulss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

// VUCOMISS sets CF on either less-than or unordered, and ZF on equality/unordered.
// The original FOV branches use JBE/CMOVBE, so unordered follows the <= arm.
inline bool unordered_or_less_equal(float left, float right)
{
    uint8_t result;
    asm volatile("vucomiss %2, %1; setbe %0" : "=qm"(result) : "x"(left), "x"(right) : "cc");
    return result != 0;
}

__attribute__((target("sse4.1"))) inline Vec4 with_y(Vec4 original, float y)
{
    constexpr int replace_y = 0x10;
    return Vec4(_mm_insert_ps(original.v, _mm_set_ss(y), replace_y));
}

} // namespace engine::camera_parameter_math
