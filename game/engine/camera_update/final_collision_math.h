// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera final collision. Include through math.h.
#pragma once
namespace engine::camera_final_collision_math
{
using namespace engine::camera_wing_math;
inline float maximum(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vmaxss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}
} // namespace engine::camera_final_collision_math
