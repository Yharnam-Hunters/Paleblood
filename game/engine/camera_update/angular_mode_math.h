// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera angular mode. Include through math.h.
#pragma once
namespace engine::camera_angular_mode_math
{

// Only the result's low lane is consumed. The original VSQRTSS source1 has delta.Y low,
// while source2 is the ordered X²+Z² sum; preserve those low-lane roles explicitly.
inline float square_root(float merge, float squared_length)
{
    float result;
    asm volatile("%{vex3%} vsqrtss %2, %1, %0" : "=x"(result) : "x"(merge), "x"(squared_length));
    return result;
}

inline float horizontal_length(Vec4 difference)
{
    const float x_squared = camera_control_math::multiply(difference.x(), difference.x());
    const float z_squared = camera_control_math::multiply(difference.z(), difference.z());
    const float squared_length = camera_target_math::add(x_squared, z_squared);
    return square_root(difference.y(), squared_length);
}

inline float xor_low(float value, Vec4 mask)
{
    return _mm_cvtss_f32(_mm_xor_ps(_mm_set_ss(value), mask.v));
}

} // namespace engine::camera_angular_mode_math
