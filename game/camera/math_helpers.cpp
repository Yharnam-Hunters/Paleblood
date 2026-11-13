// SPDX-License-Identifier: GPL-2.0-or-later
// Camera: two math helpers the follow-camera update calls through math_calls.h when it builds
// the subject's transform (update/subject_transform.h).
// - camera_axis_angle_quaternion_00848e80: the rotation quaternion of an angle about an axis,
//   (axis * sin(angle/2), cos(angle/2)). The axis is used as given (not normalized) and its w
//   lane is dropped. The sine and cosine come from the engine's octant polynomial, which clamps
//   to 1 and so turns a NaN or infinite angle into a sine of +-1 and a cosine of +-1.
// - camera_transform_matrix_0083d4d0: a 4x4 transform from a position, a rotation quaternion
//   and a per-axis scale: the quaternion's rotation rows multiplied by the scale (as a diagonal
//   matrix, so an infinite or NaN scale spreads NaN through the 0 entries), the position as the
//   last row, then the original's affine finish (w lanes 0, 0, 0, 1).
#include "../engine/octant_sine_cosine.h"
#include "../engine/vector_operations.h"
#include "update/math_calls.h"
#include "runtime/original.h"

namespace {

using engine::Vec4;

// Three rows of a 3x3 matrix (w lanes ride along).
struct alignas(16) Rows3 {
    Vec4 x;
    Vec4 y;
    Vec4 z;
};
static_assert(sizeof(Rows3) == 0x30);

// The rotation rows of a unit quaternion.
RT_ORIGINAL(0x00829b00, quaternion_rotation_rows, void(Rows3 *output, const Vec4 *quaternion));
// output row i = right.i.x * left.x + right.i.y * left.y + right.i.z * left.z (right * left).
RT_ORIGINAL(0x0084b9d0, rows3_multiply, void(Rows3 *output, const Rows3 *left, const Rows3 *right));
// Copies a 4x4 matrix with the w lanes of the three axis rows set to 0 and the position's to 1.
RT_ORIGINAL(0x0083b7e0, matrix4_affine_finish, void(const camera::Matrix4 *source, camera::Matrix4 *output));


}  // namespace

extern "C" void bb_camera_axis_angle_quaternion_00848e80(Vec4 *output, const Vec4 *axis, float angle)
{
    constexpr float half = 0.5f;
    const Vec4 half_angle = engine::vector_operations::broadcast(angle) * engine::vector_operations::broadcast(half);
    const engine::rotation::SineCosine half_turn = engine::octant::sine_cosine(half_angle);
    *output = engine::vector_operations::multiply_in_order(half_turn.sine, *axis).with_w_of(half_turn.cosine);
}

extern "C" void bb_camera_transform_matrix_0083d4d0(const camera::math_calls::TransformInput48 *source,
                                                     camera::Matrix4 *output)
{
    using engine::vector_operations::lanes;
    Rows3 rotation;
    quaternion_rotation_rows(&rotation, &source->quaternion);
    const Vec4 scale = source->scale;
    // The scale matrix: each identity row times the scale (0 times an infinite scale is NaN).
    const Rows3 scaling{scale * lanes(1.0f, 0.0f, 0.0f, 0.0f), scale * lanes(0.0f, 1.0f, 0.0f, 0.0f),
                        scale * lanes(0.0f, 0.0f, 1.0f, 0.0f)};
    Rows3 scaled;
    rows3_multiply(&scaled, &rotation, &scaling);
    const camera::Matrix4 unfinished{scaled.x, scaled.y, scaled.z, source->position};
    matrix4_affine_finish(&unfinished, output);
}
