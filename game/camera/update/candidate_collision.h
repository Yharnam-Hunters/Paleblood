// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera candidate collision stage, included only by update.cpp.
#pragma once
#include "math.h"
// Clamp the fulcrum with a sphere cast, derive camera distance, then cast the candidate.
#include "query_calls.h"

namespace camera::update::candidate_collision
{
namespace math = camera::update::math;
namespace root = camera::update::root;
namespace calls = camera::query_calls;

RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_GLOBAL(0x04d25e88, distance_division_threshold, const float);
RT_GLOBAL(0x04d25e90, minimum_fulcrum_hit_distance, const float);

void prepare_candidate_collision(FollowCamera &state, QueryWorld *queries, Vec4 fulcrum)
{
    // Keep the prepared fulcrum for a skipped query. A called query's miss path instead
    // reloads guest fulcrum, since the callback can alter the camera through another alias.
    if (wall_camera_mode.get() == 0 && queries != nullptr)
    {
        Vec4 query_origin = state.cast_origin;
        const Vec4 saved_origin = query_origin;
        const Vec4 saved_displacement = math::subtract(fulcrum, query_origin);
        Vec4 query_displacement = saved_displacement;
        Vec4 hit_position;
        float hit_fraction;
        const float radius = state.cast_sphere_radius;
        const uint8_t hit =
            calls::sphere_query(queries, calls::follow_camera_query_filter, &query_origin,
                                &query_displacement, radius, &hit_position, nullptr, &hit_fraction);
        if (hit != 0)
        {
            const Vec4 squared_length = math::squared_xyz_length(saved_displacement);

            const Vec4 length_vector = root::refined_length(squared_length,
                                                            root::FirstProductOrder::reciprocal_then_length);
            const float length = length_vector.x();
            // Retain the literal length*fraction / length and length*ratio sequence.
            // Cancelling it changes zero, infinity, NaN and ordinary binary32 rounding.
            const float scaled_length = math::multiply(length, hit_fraction);
            float ratio = math::divide(scaled_length, length);
            const float hit_distance = math::multiply(length, ratio);
            const float minimum = minimum_fulcrum_hit_distance.get();
            if (math::ordered_greater(minimum, hit_distance)) ratio = math::divide(minimum, length);
            const Vec4 displacement = math::multiply(saved_displacement, math::broadcast(ratio));
            fulcrum = math::add(saved_origin, displacement);
            state.fulcrum = fulcrum;
        }
        else
        {
            fulcrum = state.fulcrum;

        }
    }

    const Vec4 origin = state.cast_origin;
    const Vec4 difference = math::subtract(fulcrum, origin);
    const Vec4 length_vector = root::refined_length(math::squared_xyz_length(difference),
                                                    root::FirstProductOrder::reciprocal_then_length);
    const float length = length_vector.x();
    const float threshold = distance_division_threshold.get();
    float ratio;
    if (math::ordered_greater(length, threshold))
    {
        ratio = math::divide(state.camera_distance, length);
    }
    else
    {
        ratio = scalar_one.get();
    }
    const Vec4 candidate = math::add(origin, math::multiply(difference, math::broadcast(ratio)));
    state.candidate_position = candidate;

    // Read this global again after the first query, rather than retaining the first gate.
    if (wall_camera_mode.get() == 0 && queries != nullptr)
    {
        Vec4 query_origin = state.cast_origin;
        const Vec4 saved_origin = query_origin;
        const Vec4 saved_displacement = math::subtract(candidate, query_origin);
        Vec4 query_displacement = saved_displacement;
        Vec4 hit_position;
        float hit_fraction;
        const float radius = state.cast_sphere_radius;
        const uint8_t hit =
            calls::sphere_query(queries, calls::follow_camera_query_filter, &query_origin,
                                &query_displacement, radius, &hit_position, nullptr, &hit_fraction);
        if (hit != 0)
        {
            const Vec4 squared_length = math::squared_xyz_length(saved_displacement);

            const Vec4 hit_length = root::refined_length(squared_length,
                                                         root::FirstProductOrder::reciprocal_then_length);
            const float hit_distance = math::multiply(hit_length.x(), hit_fraction);
            const float minimum = distance_division_threshold.get();
            const float distance = math::maximum(minimum, hit_distance);
            const Vec4 scaled = math::multiply(saved_displacement, math::broadcast(distance));
            const Vec4 reciprocal_length =
                math::divide(unit_vector.get(), math::broadcast<0>(hit_length));
            const Vec4 displacement = math::multiply(reciprocal_length, scaled);
            state.candidate_position = math::add(saved_origin, displacement);
        }
        // A miss retains callback changes to guest candidate; no reload/publication occurs.
    }
}

} // namespace camera::update::candidate_collision
