// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera target. Include through math.h.
#pragma once
namespace engine::camera_target_math
{

inline float add(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vaddss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline Vec4 broadcast(float value)
{
    return Vec4(_mm_set1_ps(value));
}

} // namespace engine::camera_target_math
