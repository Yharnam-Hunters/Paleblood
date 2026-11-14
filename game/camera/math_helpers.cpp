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
// - camera_quaternion_rotation_rows_00829b00: the rotation rows of a quaternion (x, y, z, w),
//   from the products x*2x, y*2y, x*2y, ... : (1-2zz-2yy, 2wz+2xy, 2xz-2wy), (2xy-2wz,
//   1-2zz-2xx, 2yz+2wx) and (2xz+2wy, 2yz-2wx, 1-2xx-2yy). The first two rows' w lanes carry the
//   third row's x and y; the third's w lane is 0.
// - camera_rows3_multiply_0084b9d0: output row i = left.x * right.i.x + left.y * right.i.y +
//   left.z * right.i.z, every lane (w too), the z row first.
// - camera_matrix4_affine_finish_0083b7e0: copies a 4x4 matrix with the three axis rows' w lanes
//   set to 0 and the position's to 1.
// These three follow the original's operations lane by lane and in its operand order, so NaN
// payloads come out as they do there; the matrix's output may be its source, and the rows'
// output its left or right rows, as the left rows are read first and the right row by row.
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

namespace ops = engine::vector_operations;

// The original's movss: the low lane of `low`, the other three of `rest` (two moves of words:
// (low.x, low.x, rest.y, rest.y), then its first and third words with rest's z and w).
constexpr int pick_low_x_rest_y = 0x50;   // (first[0], first[0], second[1], second[1])
constexpr int pick_x_y_rest_zw = 0xe8;    // (first[0], first[2], second[2], second[3])
Vec4 low_lane_from(Vec4 rest, Vec4 low)
{
    return ops::shuffle_two<pick_x_y_rest_zw>(ops::shuffle_two<pick_low_x_rest_y>(low, rest), rest);
}

// One row of a product: left.x * row.x + left.y * row.y + left.z * row.z, as the original adds.
Vec4 row_times(const Rows3 &left, Vec4 row)
{
    const Vec4 x = ops::multiply_in_order(left.x, ops::broadcast<0>(row));
    const Vec4 y = ops::multiply_in_order(left.y, ops::broadcast<1>(row));
    const Vec4 z = ops::multiply_in_order(left.z, ops::broadcast<2>(row));
    return ops::add_in_order(z, ops::add_in_order(x, y));
}

// The quaternion's words picked for its products (pshufd orders): (y, x, x, y) and
// (2y, 2y, 2z, 2z); (z, w, w, w) and (2z, 2z, 2y, 2x).
constexpr int pick_yxxy = 0x41, pick_yyzz = 0xa5, pick_zwww = 0xfe, pick_zzyx = 0x1a;
constexpr int swap_pairs = 0xb1;   // (1, 0, 3, 2)
constexpr int third_row = 0x8d;    // (first[1], first[3], second[0], second[2])

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

extern "C" void bb_camera_quaternion_rotation_rows_00829b00(Rows3 *output, const Vec4 *quaternion)
{
    const Vec4 unit_x = ops::lanes(1.0f, 0.0f, 0.0f, 0.0f);
    const Vec4 flip_yzw = ops::lanes(0.0f, -0.0f, -0.0f, -0.0f);
    const Vec4 flip_xzw = ops::lanes(-0.0f, 0.0f, -0.0f, -0.0f);
    const Vec4 q = *quaternion;
    const Vec4 twice = ops::add_in_order(q, q);
    // (y*2y, x*2y, x*2z, y*2z), (z*2z, w*2z, w*2y, w*2x) and x*2x in the low lane.
    const Vec4 a = ops::multiply_in_order(ops::shuffle_words<pick_yxxy>(q), ops::shuffle_words<pick_yyzz>(twice));
    const Vec4 b = ops::multiply_in_order(ops::shuffle_words<pick_zwww>(q), ops::shuffle_words<pick_zzyx>(twice));
    const Vec4 xx = ops::multiply_in_order(q, twice);
    // (1 - x*2x - y*2y, 0, 0, 0)
    const Vec4 third_z = low_lane_from(unit_x, ops::subtract(ops::subtract(unit_x, xx), a));
    const Vec4 a_flipped = ops::bit_xor(a, flip_yzw);
    const Vec4 b_flipped = ops::bit_xor(b, flip_xzw);
    const Vec4 sums = low_lane_from(b_flipped, ops::add_in_order(b_flipped, unit_x));
    const Vec4 row_x = ops::subtract(sums, a_flipped);
    const Vec4 row_y = ops::shuffle_words<swap_pairs>(
        ops::subtract(ops::bit_xor(sums, flip_yzw), low_lane_from(a_flipped, xx)));
    output->x = row_x;
    output->y = row_y;
    output->z = ops::shuffle_two<third_row>(ops::high_halves(row_x, row_y), third_z);
}

extern "C" void bb_camera_rows3_multiply_0084b9d0(Rows3 *output, const Rows3 *left, const Rows3 *right)
{
    const Rows3 rows = *left;
    output->z = row_times(rows, right->z);
    output->y = row_times(rows, right->y);
    output->x = row_times(rows, right->x);
}

extern "C" void bb_camera_matrix4_affine_finish_0083b7e0(const camera::Matrix4 *source, camera::Matrix4 *output)
{
    const Vec4 zero = ops::broadcast(0.0f);
    const Vec4 one = ops::broadcast(1.0f);
    const camera::Matrix4 matrix = *source;
    output->axis_x = matrix.axis_x.with_w_of(zero);
    output->axis_y = matrix.axis_y.with_w_of(zero);
    output->forward = matrix.forward.with_w_of(zero);
    output->position = matrix.position.with_w_of(one);
}
