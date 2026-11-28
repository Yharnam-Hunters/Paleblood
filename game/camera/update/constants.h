// SPDX-License-Identifier: GPL-2.0-or-later
// Guest constants that more than one follow-camera stage reads. Each address is declared once here;
// a stage whose role for the constant has its own name aliases it (one declaration per address).
#pragma once
#include "math.h"
#include "runtime/original.h"

namespace camera::update::constants
{
RT_GLOBAL(0x04cf24a0, unit_y, const Vec4);
RT_GLOBAL(0x04cf24b0, unit_z, const Vec4);
RT_GLOBAL(0x04cf24c0, unit_w, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24e0, negate_yaw_sine, const Vec4);
RT_GLOBAL(0x04cf2520, sign_mask, const Vec4);
RT_GLOBAL(0x04cf25c0, even_coefficients_0_to_6, const Vec4);
RT_GLOBAL(0x04cf25d0, even_coefficients_8_to_14, const Vec4);
RT_GLOBAL(0x04cf25e0, even_coefficients_16_to_22, const Vec4);
RT_GLOBAL(0x04cf2630, negate_pitch_sine, const Vec4);
RT_GLOBAL(0x04cf2640, exponent_bits, const Vec4);
RT_GLOBAL(0x04cf2650, canonical_nan_vector, const Vec4);
RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04cf2680, short_collision_displacement, const Vec4);
RT_GLOBAL(0x04d25e58, degrees_to_radians, const float);
RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_GLOBAL(0x04d25e70, half_fov, const float);
RT_GLOBAL(0x04d25e74, two_pi, const float);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);
RT_GLOBAL(0x04d25e8c, short_distance_replacement, const float);
}  // namespace camera::update::constants
