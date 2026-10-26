// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera chase. Include through math.h.
#pragma once
namespace engine::camera_chase_math
{
using namespace engine::camera_wing_math;

inline float dot_xyz(Vec4 left, Vec4 right)
{
    const Vec4 products = multiply(left, right);
    const Vec4 pairs = horizontal_add(products);
    return add(pairs, broadcast<Vec4::lane_z>(products)).x();
}

} // namespace engine::camera_chase_math
