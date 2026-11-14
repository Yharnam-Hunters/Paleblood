// SPDX-License-Identifier: GPL-2.0-or-later
// AI: hearing. The hearing test (0x0169d160) decides whether an enemy hears one sound and, when
// it does, remembers where the sound came from; the ear's shape (0x0169da30) answers whether a
// sphere around a point reaches the enemy's ear.
//
// The hearing test, in plain words:
// - A sound carries for its range, less the enemy's sound-cut distance (NpcThinkParam
//   ear_soundcut_dist, whole metres). Nothing is heard when that leaves no range.
// - The gap to the sound is its distance from the enemy, less the enemy's size and the size of
//   the character that made it. A sound whose remaining range covers the gap is heard.
// - Otherwise the ear may still reach it: the ear reaches ear_dist / 100 times the listener's
//   hearing scale further. With an ear that hears all around (ear_angY 180 degrees or more) and
//   at every height (ear_angX 90 or more), reach plus range against the gap decides. Without a
//   row the same test runs with no reach, so nothing out of range is heard. All around but not
//   at every height, the ear's shape decides.
// - With a narrower ear, a sound inside the ear's width angle ahead of the enemy goes to the
//   ear's shape. A sound outside it is heard only near the edge of the width angle on the
//   sound's side: the point on that edge closest to the sound (on the ground, at a height that
//   adds the sound's height to the ear's) must lie within the remaining range, and the sphere
//   the ear's shape is asked about is the part of the range left at that point.
// - A heard sound is remembered: its position (unless the enemy already remembers a position
//   closer than this sound's gap) and, always, its AiSound id.
//
// The ear's shape: the ear is a shape around the listener's position, with its height angle from
// the enemy's NpcThinkParam row (ear_angX, whole degrees). Without a row the angle is 90 degrees,
// whose tangent in float is a large negative number, so every point counts as above or below it.
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
// The parameter names are the community's names for the NpcThinkParam fields (Paramdex); no
// values are recorded here.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/rotation_math.h"
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

// The part of an NpcThinkParam row hearing reads.
struct ThinkParamHearing {
    uint8_t unknown_0x00[0x32];
    uint16_t ear_dist;            // the ear's reach, in hundredths of the listener's hearing scale
    uint16_t ear_soundcut_dist;   // taken off every sound's range, whole metres
    uint8_t unknown_0x36[0x1d];
    uint8_t ear_ang_x;            // height angle, whole degrees
    uint8_t ear_ang_y;            // width angle (each side of ahead), whole degrees
};
static_assert(offsetof(ThinkParamHearing, ear_dist) == 0x32 && offsetof(ThinkParamHearing, ear_soundcut_dist) == 0x34);
static_assert(offsetof(ThinkParamHearing, ear_ang_x) == 0x53 && offsetof(ThinkParamHearing, ear_ang_y) == 0x54);

// What the enemy's character returns for its think parameters: the row, or null (the holder
// sight.cpp reads as well).
struct ThinkParamHolder {
    uint8_t unknown_0x00[0x8];
    const ThinkParamHearing *row;
};
static_assert(offsetof(ThinkParamHolder, row) == 0x8);

// The enemy's character, as hearing asks it through its vtable.
struct HearingOwner;
struct HearingOwnerVtable {
    void *unknown_slots_0x00[0x18 / 8];
    Vec4 (*position)(HearingOwner *);
    void *unknown_slot_0x20;
    Vec4 (*forward)(HearingOwner *);
    void *unknown_slots_0x30[(0x40 - 0x30) / 8];
    float (*size)(HearingOwner *);
};
static_assert(offsetof(HearingOwnerVtable, position) == 0x18 && offsetof(HearingOwnerVtable, forward) == 0x28);
static_assert(offsetof(HearingOwnerVtable, size) == 0x40);
struct HearingOwner {
    const HearingOwnerVtable *vtable;
};

// What the enemy remembers of the last sound it heard.
struct HeardSound;
struct HeardSoundVtable {
    void *unknown_slots_0x00[0x28 / 8];
    bool (*remembers_position)(HeardSound *);   // true: a position to compare with is kept
    void *unknown_slots_0x30[(0x80 - 0x30) / 8];
    Vec4 (*remembered_position)(HeardSound *);
};
static_assert(offsetof(HeardSoundVtable, remembers_position) == 0x28);
static_assert(offsetof(HeardSoundVtable, remembered_position) == 0x80);
struct HeardSound {
    const HeardSoundVtable *vtable;
    uint8_t unknown_0x08[0x8];
    Vec4 position;
    uint8_t heard;
    uint8_t unknown_0x21[0x3];
    int32_t ai_sound_id;
};
static_assert(offsetof(HeardSound, position) == 0x10 && offsetof(HeardSound, heard) == 0x20);
static_assert(offsetof(HeardSound, ai_sound_id) == 0x24);

// The hearing test's object: the enemy, its think parameters, its memory of sounds.
struct Listener {
    uint8_t unknown_0x00[0x8];
    HearingOwner *owner;
    const ThinkParamHolder *think_params;
    HeardSound *memory;
    float hearing_scale;          // multiplies the ear's reach
};
static_assert(offsetof(Listener, owner) == 0x8 && offsetof(Listener, think_params) == 0x10);
static_assert(offsetof(Listener, memory) == 0x18 && offsetof(Listener, hearing_scale) == 0x20);

// One sound, as the hearing test reads it.
struct Sound {
    uint8_t unknown_0x00[0x10];
    Vec4 position;
    uint8_t unknown_0x20[0x8];
    float range;
    uint8_t unknown_0x2c[0x4];
    int32_t ai_sound_id;          // the sound's AiSound param row
};
static_assert(offsetof(Sound, position) == 0x10 && offsetof(Sound, range) == 0x28);
static_assert(offsetof(Sound, ai_sound_id) == 0x30);

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

extern "C" bool bb_ai_hearing_in_ear_range(const Listener *listener, const Vec4 *listener_position, float reach,
                                           Vec4 point, float radius);

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

namespace {

constexpr float ear_dist_divisor = 100.0f;      // ear_dist is in hundredths
constexpr uint32_t half_pi_bits = 0x3fc90fdb;   // ear_angX at least this: every height
constexpr float one_side = 1.0f, other_side = -1.0f;

float radians(uint8_t degrees) { return engine::multiply(static_cast<float>(degrees), degrees_to_radians); }

// The distance from `from` to `to`, by the refined root.
float distance_between(Vec4 from, Vec4 to) { return length3(vec::subtract(to, from)); }

// A unit vector along `value` (lanes as the original computes them).
Vec4 unit(Vec4 value)
{
    const Vec4 squared = squared_length4(value);
    return normalized(value, squared, refined_length(squared));
}

// The sum of the four lanes of value times weights (the weights' lanes as given).
float weighted_sum(Vec4 value, Vec4 weights) { return group_sum(vec::multiply(value, weights)); }

// `facing` turned by `angle` about the vertical (its w lane rides along).
Vec4 turned(Vec4 facing, float angle)
{
    const engine::rotation::SineCosine turn = engine::rotation::sine_cosine(vec::broadcast(angle));
    const float sine = turn.sine.x(), cosine_ = turn.cosine.x();
    const float negative_sine = vec::multiply(turn.sine, vec::broadcast(other_side)).x();
    return vec::lanes(weighted_sum(facing, vec::lanes(cosine_, 0.0f, sine, 0.0f)),
                      weighted_sum(facing, vec::lanes(0.0f, 1.0f, 0.0f, 0.0f)),
                      weighted_sum(facing, vec::lanes(negative_sine, 0.0f, cosine_, 0.0f)),
                      vec::broadcast<lane_w>(facing).x());
}

// What one call of the hearing test works with.
struct Hearing {
    Listener *listener;
    const Sound *sound;
    Vec4 ear;            // the enemy's position
    Vec4 offset;         // from the ear to the sound
    float gap;           // the distance, less both sizes
    float range;         // the sound's range, less the sound-cut distance
    float reach;         // how much further the ear reaches
};

bool in_ear_range(const Hearing &h, Vec4 point, float radius)
{
    return bb_ai_hearing_in_ear_range(h.listener, &h.ear, h.reach, point, radius);
}

// Outside the width angle: the sound is heard only near the edge of the angle on its side.
bool heard_beside_edge(const Hearing &h)
{
    const Vec4 flat_offset = flattened(h.offset);
    const Vec4 toward_sound = unit(flat_offset);
    HearingOwner *owner = h.listener->owner;
    const Vec4 facing = unit(flattened(owner->vtable->forward(owner)));
    // The vertical part of the cross product: which side of the facing the sound lies on.
    const float side = engine::subtract(engine::multiply(toward_sound.x(), facing.z()),
                                        engine::multiply(toward_sound.z(), facing.x()));
    const ThinkParamHearing *row = h.listener->think_params->row;
    const float width_angle = row ? radians(row->ear_ang_y) : std::bit_cast<float>(pi_bits);
    const float turn = engine::multiply(vec::ordered_at_least(side, 0.0f) ? one_side : other_side, width_angle);
    const Vec4 edge = turned(facing, turn);

    // The sound's distance from the edge line, on the ground, against the remaining range.
    const float along = dot3(flat_offset, edge);
    const float off_edge =
        vec::square_root(engine::subtract(dot3(flat_offset, flat_offset), engine::multiply(along, along)));
    if (!vec::ordered_at_least(h.range, off_edge)) return false;
    const float range_left = vec::square_root(
        engine::subtract(engine::multiply(h.range, h.range), engine::multiply(off_edge, off_edge)));
    const Vec4 on_edge = vec::add(vec::multiply(edge, vec::broadcast(along)), h.ear);
    const Vec4 closest = vec::add(on_edge, vec::lanes(0.0f, h.sound->position.y(), 0.0f, 0.0f));
    return in_ear_range(h, closest, range_left);
}

// A sound whose range does not cover the gap: whether the ear still reaches it.
bool heard_by_ear(const Hearing &h, const ThinkParamHearing *row)
{
    if (!row) return vec::ordered_greater(engine::add(h.reach, h.range), h.gap);
    const bool every_height = !(radians(row->ear_ang_x) < std::bit_cast<float>(half_pi_bits));
    const float width_angle = radians(row->ear_ang_y);
    if (vec::ordered_at_least(width_angle, std::bit_cast<float>(pi_bits))) {
        if (every_height) return vec::ordered_greater(engine::add(h.reach, h.range), h.gap);
        return in_ear_range(h, h.sound->position, h.range);
    }

    const float width_cosine = cosine(wrapped_once(width_angle, true));
    const Vec4 toward = vec::multiply(h.offset, vec::divide(vec::broadcast(1.0f), vec::broadcast(h.gap)));
    HearingOwner *owner = h.listener->owner;
    if (vec::ordered_at_least(dot3(toward, owner->vtable->forward(owner)), width_cosine))
        return in_ear_range(h, h.sound->position, h.range);
    return heard_beside_edge(h);
}

// The distance from the enemy to the sound, less the enemy's size and the source's size.
float gap_to(HearingOwner *owner, Vec4 ear, Vec4 sound_position, float source_size)
{
    const float distance = distance_between(ear, sound_position);
    return engine::subtract(engine::subtract(distance, owner->vtable->size(owner)), source_size);
}

// Remembers a heard sound: its position, unless a remembered position is closer than this
// sound's gap; always its AiSound id.
void remember(Listener *listener, const Sound *sound, float source_size)
{
    bool replace = true;
    if (listener->memory->vtable->remembers_position(listener->memory)) {
        HearingOwner *owner = listener->owner;
        const Vec4 ear = owner->vtable->position(owner);
        const float gap = gap_to(owner, ear, sound->position, source_size);
        HeardSound *memory = listener->memory;
        replace = vec::ordered_greater(distance_between(ear, memory->vtable->remembered_position(memory)), gap);
    }
    if (replace) {
        listener->memory->position = sound->position;
        listener->memory->heard = 1;
    }
    listener->memory->ai_sound_id = sound->ai_sound_id;
}

}  // namespace

// The hearing test: whether the enemy hears `sound` (the caller has checked that the sound can be
// heard from where the enemy is: `audible`), remembering it when it does. `source_size` is the
// size of the character that made the sound (0 for others).
extern "C" void bb_ai_hearing_test(Listener *listener, const Sound *sound, bool audible, float source_size)
{
    if (!audible) return;
    const ThinkParamHearing *row = listener->think_params->row;
    const float range = engine::subtract(sound->range, row ? static_cast<float>(row->ear_soundcut_dist) : 0.0f);
    if (vec::ordered_at_least(0.0f, range)) return;

    Hearing h{listener, sound, {}, {}, 0.0f, range, 0.0f};
    const float hearing_scale = listener->hearing_scale;
    HearingOwner *owner = listener->owner;
    h.ear = owner->vtable->position(owner);
    h.offset = vec::subtract(sound->position, h.ear);
    h.gap = gap_to(owner, h.ear, sound->position, source_size);
    row = listener->think_params->row;
    const float ear_scale = row ? engine::divide(static_cast<float>(row->ear_dist), ear_dist_divisor) : 0.0f;
    if (!vec::ordered_greater(range, h.gap)) {
        h.reach = engine::multiply(ear_scale, hearing_scale);
        if (!heard_by_ear(h, row)) return;
    }
    remember(listener, sound, source_size);
}

}  // namespace ai
