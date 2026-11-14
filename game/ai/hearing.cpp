// SPDX-License-Identifier: GPL-2.0-or-later
// AI: hearing, the ear's shape. The hearing test (0x0169d160, left original) asks this function
// whether a sphere around a point of a sound reaches the enemy's ear.
//
// The ear is a shape around the listener's position, with its height angle from the enemy's
// NpcThinkParam row (ear_angX, whole degrees). Without a row the angle is 90 degrees, whose
// tangent in float is a large negative number, so every point counts as above or below it.
//
// - A point closer than 0.0001 to the ear is heard.
// - Within the height angle (the point's height over its distance on the ground is at most the
//   angle's tangent), the sphere is heard when it reaches within `reach` of the ear: the distance
//   to the point is less than reach plus the sphere's radius.
// - Above or below that angle, the ear reaches only along the edge of the angle: a segment
//   `reach` long from the listener towards the point on the ground, tilted up (or down) to the
//   angle. The sphere is heard when its surface crosses that segment: the segment passes closer
//   to the point than the radius, and the listener or the segment's far end lies outside the
//   sphere. A sphere that holds the whole segment is not heard.
//
// The parameter name is the community's name for the NpcThinkParam field (Paramdex); no values
// are recorded here.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/scalar.h"
#include "../engine/vector.h"
#include "../engine/vector_operations.h"
#include "perception_math.h"
#include "runtime/original.h"

namespace ai {

using engine::Vec4;
namespace vec = engine::vector_operations;
using namespace perception_math;

namespace {

// tanf, through the executable's import thunk.
RT_ORIGINAL(0x02fbe648, tangent, float(float));

// The part of an NpcThinkParam row the ear reads.
struct ThinkParamHearing {
    uint8_t unknown_0x00[0x53];
    uint8_t ear_ang_x;            // height angle, whole degrees
};
static_assert(offsetof(ThinkParamHearing, ear_ang_x) == 0x53);

// What the enemy's character returns for its think parameters: the row, or null (the holder
// sight.cpp reads as well).
struct ThinkParamHolder {
    uint8_t unknown_0x00[0x8];
    const ThinkParamHearing *row;
};
static_assert(offsetof(ThinkParamHolder, row) == 0x8);

// The hearing test's object, with the field the ear reads.
struct Listener {
    uint8_t unknown_0x00[0x10];
    const ThinkParamHolder *think_params;
};
static_assert(offsetof(Listener, think_params) == 0x10);

constexpr float at_the_ear = 0.0001f;          // closer than this is always heard
constexpr float flat_length_epsilon = 0.00001f;  // a shorter ground direction is no direction
constexpr uint32_t right_angle_bits = 0x3fc90fdb;  // pi / 2, the height angle without a row

float length3(Vec4 value) { return refined_length(squared_length3(value)).x(); }

float ear_height_angle(const Listener *listener)
{
    const ThinkParamHearing *row = listener->think_params->row;
    if (!row) return std::bit_cast<float>(right_angle_bits);
    return engine::multiply(static_cast<float>(row->ear_ang_x), degrees_to_radians);
}

// The ground direction from the listener towards the point, of length one; zero when the point
// is (almost) straight above or below.
Vec4 ground_direction(Vec4 flat, float flat_length)
{
    if (!vec::ordered_greater(flat_length, flat_length_epsilon)) return Vec4::zero();
    return vec::multiply(flat, vec::divide(vec::broadcast(1.0f), vec::broadcast(flat_length)));
}

// From the closest point of the segment [0, edge] to `offset`, to `offset`.
Vec4 from_closest_point(Vec4 offset, Vec4 edge)
{
    const float along = dot3(offset, edge);
    if (vec::ordered_at_least(0.0f, along)) return offset;
    const float edge_squared = dot3(edge, edge);
    if (vec::ordered_at_least(along, edge_squared)) return vec::subtract(offset, edge);
    return vec::subtract(offset, vec::multiply(edge, vec::broadcast(engine::divide(along, edge_squared))));
}

}  // namespace

// True when the sphere of `radius` around `point` reaches the ear at *listener_position.
extern "C" bool bb_ai_hearing_in_ear_range(const Listener *listener, const Vec4 *listener_position, float reach,
                                           Vec4 point, float radius)
{
    const Vec4 ear = *listener_position;
    const Vec4 offset = vec::subtract(point, ear);
    const float distance = length3(offset);
    if (vec::ordered_greater(at_the_ear, distance)) return true;

    const Vec4 flat = vec::lanes(offset.x(), 0.0f, offset.z(), 0.0f);
    const float flat_length = length3(flat);
    const Vec4 direction = ground_direction(flat, flat_length);
    const float slope = tangent(ear_height_angle(listener));
    const float height = vec::absolute(offset.y());
    if (vec::ordered_at_least(engine::multiply(flat_length, slope), height))
        return vec::ordered_greater(engine::add(reach, radius), distance);

    // The edge of the height angle on the point's side (up when the point is above; below, every
    // lane of the tilt has its sign flipped, as in the original).
    const float rising = engine::multiply(offset.y(), engine::divide(1.0f, distance));
    Vec4 tilt = vec::lanes(0.0f, slope, 0.0f, 0.0f);
    if (!vec::ordered_greater(rising, 0.0f)) tilt = vec::bit_xor(tilt, vec::broadcast(-0.0f));
    const Vec4 edge = vec::multiply(vec::broadcast(reach), vec::add(direction, tilt));

    const float radius_squared = engine::multiply(radius, radius);
    const Vec4 miss = from_closest_point(offset, edge);
    if (!vec::ordered_greater(radius_squared, dot3(miss, miss))) return false;
    if (vec::ordered_at_least(dot3(offset, offset), radius_squared)) return true;
    const Vec4 from_far_end = vec::subtract(point, vec::add(ear, edge));
    return vec::ordered_at_least(dot3(from_far_end, from_far_end), radius_squared);
}

}  // namespace ai
