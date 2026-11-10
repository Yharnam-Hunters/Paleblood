// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera final collision stage, included only by update.cpp.
#pragma once
#include "math.h"
#include "orientation.h"

namespace camera::update::final_collision
{
namespace math = camera::update::math;
namespace root = camera::update::root;
namespace direction = camera::update::wing_escape;
namespace calls = camera::query_calls;
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04cf2680, short_collision_displacement, const Vec4);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);

void collide_final_camera(FollowCamera &state, QueryWorld *queries, Vec4 chased_candidate)
{
    if (queries == nullptr) return;
    Vec4 query_origin = state.cast_origin;
    const Vec4 saved_origin = query_origin;
    const Vec4 saved_displacement = math::subtract(chased_candidate, query_origin);
    Vec4 query_displacement = saved_displacement;
    Vec4 hit_position;
    float fraction;
    const float radius = state.cast_sphere_radius;
    const uint8_t hit = calls::sphere_query(queries, calls::follow_camera_query_filter, &query_origin,
                                            &query_displacement, radius, &hit_position, nullptr, &fraction);
    if (hit == 0) return;
    const Vec4 squared = math::squared_xyz_length(saved_displacement);

    const Vec4 length =
        root::refined_length(squared, root::FirstProductOrder::reciprocal_then_length);
    const float threshold = distance_threshold.get();
    Vec4 displacement;
    if (math::ordered_greater(threshold, length.x()))
    {
        const Vec4 candidate = state.candidate_position;
        const Vec4 cast = state.cast_origin;
        const Vec4 direction_input =
            math::bit_and(math::subtract(candidate, cast), xyz_bits.get());
        const Vec4 normalized = direction::normalize_direction(
            direction_input, direction::direction_squared_length(direction_input));
        displacement = math::multiply(normalized, short_collision_displacement.get());
    }
    else
    {
        const float hit_distance = math::multiply(length.x(), fraction);
        const float clamped_distance = math::maximum(threshold, hit_distance);
        const Vec4 scaled = math::multiply(saved_displacement, math::broadcast(clamped_distance));
        const Vec4 inverse = math::divide(unit_vector.get(), math::broadcast<0>(length));
        displacement = math::multiply(inverse, scaled);
    }
    state.transform.position = math::add(saved_origin, displacement);
}

} // namespace camera::update::final_collision
