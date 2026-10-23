// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera rotation dispatch. Include through math.h.
#pragma once
namespace engine::camera_rotation_dispatch_math
{

inline float maximum(float first, float second)
{
    float result;
    // MAXSS preserves source2 for equal or unordered operands, including -0 and NaN.
    asm volatile("%{vex3%} vmaxss %2, %1, %0" : "=x"(result) : "x"(first), "x"(second));
    return result;
}

} // namespace engine::camera_rotation_dispatch_math
