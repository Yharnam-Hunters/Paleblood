// SPDX-License-Identifier: GPL-2.0-or-later
// AI: sight. How an enemy notices a character it can see, which is where the "visible" flag that
// IsVisibleTarget reads (perception.cpp) comes from.
//
// Each think update builds a sight query for every character around the enemy: the enemy's eye
// (where it stands, which way it faces, and its NpcThinkParam row) and the other character
// (where it stands, how visible it is, how dark it is around it). The sight test answers it:
//
// - Nose: a character closer than the nose distance (plus the two characters' sizes) is noticed
//   whatever the angle, even behind the enemy and through walls.
// - Eye: otherwise the character must be within the eye distance, which depends on how dark it
//   is (eye_dist, eyeDist_forDark, eyeDist_forPitchDark) and is measured from a point moved back
//   behind the enemy (eye_BackOffsetDist), but not closer than eye_BeginDist; within the width
//   angle (eye_angY, measured on the ground between the enemy's facing and the line to the
//   character) and the height angle (eye_angX, the line's elevation); and, when the caller asks,
//   a ray through the physics world from the enemy's eye to the character must hit nothing.
//
// A character found by sight is then added to the enemy's target list, or its entry refreshed,
// with the visible flag set.
//
// The parameter names are the community's names for NpcThinkParam fields (Paramdex); no values
// are recorded here.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/vector.h"
#include "../engine/vector_lane_mask.h"
#include "../engine/vector_operations.h"
#include "../engine/vector_reciprocal.h"
#include "ai_think.h"
#include "perception_math.h"

namespace ai {

using engine::Vec4;
namespace vec = engine::vector_operations;
using namespace perception_math;

namespace {

// ---- the sight query ----

// The part of an NpcThinkParam row the sight test reads. Distances are whole metres, angles
// whole degrees.
struct ThinkParamSight {
    uint8_t unknown_0x00[0x30];
    uint16_t eye_dist;                  // eye distance in light
    uint8_t unknown_0x32[0x4];
    uint16_t nose_dist;                 // noticed at any angle closer than this
    uint8_t unknown_0x38[0x19];
    uint8_t eye_ang_x;                  // height angle
    uint8_t eye_ang_y;                  // width angle
    uint8_t unknown_0x53[0x11];
    uint16_t eye_dist_for_dark;         // eye distance when the target is in the dark
    uint8_t unknown_0x66[0x2];
    uint16_t eye_dist_for_pitch_dark;   // eye distance when the target is in pitch dark
    uint8_t unknown_0x6a[0xa];
    uint16_t eye_back_offset_dist;      // the eye distance is measured from this far behind
    uint16_t eye_begin_dist;            // closer than this, the eye sees nothing
};
static_assert(offsetof(ThinkParamSight, eye_dist) == 0x30 && offsetof(ThinkParamSight, nose_dist) == 0x36);
static_assert(offsetof(ThinkParamSight, eye_ang_x) == 0x51 && offsetof(ThinkParamSight, eye_ang_y) == 0x52);
static_assert(offsetof(ThinkParamSight, eye_dist_for_dark) == 0x64);
static_assert(offsetof(ThinkParamSight, eye_dist_for_pitch_dark) == 0x68);
static_assert(offsetof(ThinkParamSight, eye_back_offset_dist) == 0x74);
static_assert(offsetof(ThinkParamSight, eye_begin_dist) == 0x76);

// What the enemy's character returns for its think parameters: the row, or null.
struct ThinkParamHolder {
    uint8_t unknown_0x00[0x8];
    const ThinkParamSight *row;
};
static_assert(offsetof(ThinkParamHolder, row) == 0x8);

// How dark it is around the target, which picks the eye distance.
constexpr uint8_t darkness_dark = 1;
constexpr uint8_t darkness_pitch_dark = 2;

// The test's answer, kept in the query.
enum class SightResult : int32_t {
    not_seen = 0,
    smelled = 1,   // within the nose distance
    seen = 2,      // by the eye
};

// One question: can the observer (the first half, filled from the enemy's character once per
// sweep) see the target (the second half, filled from each candidate character)?
struct SightQuery {
    const ThinkParamHolder *think_params;
    uint8_t unknown_0x08[0x8];
    Vec4 observer_position;
    Vec4 observer_facing;
    float observer_eye_height;      // the ray starts this far above the observer's position
    float observer_size;            // added to the nose and eye distances
    float observer_sight_scale;     // multiplies the eye distances (not the nose distance)
    uint8_t unknown_0x3c[0x4];
    Vec4 target_position;
    float target_eye_height;        // the ray ends this far above the target's position
    float target_size;              // added to the nose and eye distances
    float target_visibility;        // multiplies the eye distances; 1 after a reset
    uint8_t target_darkness;        // 0 light, 1 dark, 2 pitch dark
    uint8_t unknown_0x5d[0x3];
    SightResult result;
    uint8_t target_unseeable;       // nonzero: never noticed, whatever the test found
};
static_assert(offsetof(SightQuery, observer_position) == 0x10 && offsetof(SightQuery, observer_facing) == 0x20);
static_assert(offsetof(SightQuery, observer_eye_height) == 0x30 && offsetof(SightQuery, observer_size) == 0x34);
static_assert(offsetof(SightQuery, observer_sight_scale) == 0x38 && offsetof(SightQuery, target_position) == 0x40);
static_assert(offsetof(SightQuery, target_eye_height) == 0x50 && offsetof(SightQuery, target_size) == 0x54);
static_assert(offsetof(SightQuery, target_visibility) == 0x58 && offsetof(SightQuery, target_darkness) == 0x5c);
static_assert(offsetof(SightQuery, result) == 0x60 && offsetof(SightQuery, target_unseeable) == 0x64);

// ---- the line-of-sight ray ----

// The physics side as the ray reads it: the manager's world, and the world's ray filter.
struct RayFilterOwner {
    uint8_t unknown_0x000[0x5e8];
    void *ray_filter;
};
struct PhysicsWorld {
    uint8_t unknown_0x000[0x150];
    RayFilterOwner *filter_owner;
};
struct PhysicsWorldHolder {
    uint8_t unknown_0x00[0x8];
    PhysicsWorld *world;
};
struct PhysicsManager {
    uint8_t unknown_0x00[0x28];
    PhysicsWorldHolder *holder;
};
static_assert(offsetof(RayFilterOwner, ray_filter) == 0x5e8 && offsetof(PhysicsWorld, filter_owner) == 0x150);
static_assert(offsetof(PhysicsWorldHolder, world) == 0x8 && offsetof(PhysicsManager, holder) == 0x28);

RT_GLOBAL(0x0593d700, physics_manager_instance, PhysicsManager *);

// A ray from `origin` along `direction` (the whole length: the ray ends at origin + direction),
// with each lane's reciprocal for the slab tests. `inverse_direction`'s w lane holds, as the bits
// of a float, 0x3f000000 with one bit per lane of x, y, z that is not negative.
struct WorldRayInput {
    void *filter;
    uint16_t filter_group;
    uint8_t unknown_0x0a[0x2];
    int32_t collision_layer;
    uint64_t unknown_0x10;
    uint8_t unknown_0x18;
    uint8_t unknown_0x19[0x7];
    Vec4 origin;
    Vec4 direction;
    Vec4 inverse_direction;
    uint32_t unknown_0x50;
};
static_assert(offsetof(WorldRayInput, filter_group) == 0x8 && offsetof(WorldRayInput, collision_layer) == 0xc);
static_assert(offsetof(WorldRayInput, unknown_0x10) == 0x10 && offsetof(WorldRayInput, unknown_0x18) == 0x18);
static_assert(offsetof(WorldRayInput, origin) == 0x20 && offsetof(WorldRayInput, direction) == 0x30);
static_assert(offsetof(WorldRayInput, inverse_direction) == 0x40 && offsetof(WorldRayInput, unknown_0x50) == 0x50);

constexpr uint16_t any_filter_group = 0xffff;
constexpr int32_t sight_collision_layer = 0x29;

// The collector the world fills: has the ray hit anything? Its other fields are set as the
// original sets them; what they hold is not known yet.
struct ClosestRayHit {
    const void *vtable;
    int32_t unknown_0x08;
    uint8_t unknown_0x0c[0x4];
    Vec4 unknown_0x10;
    Vec4 unknown_0x20;
    Vec4 unknown_0x30;
    float hit_fraction;
    uint8_t unknown_0x44[0x4];
    int32_t unknown_0x48;
    uint16_t unknown_0x4c;
    uint8_t unknown_0x4e[0x2];
    int32_t unknown_0x50;
    int32_t unknown_0x54;
    uint64_t unknown_0x58;
    int32_t unknown_0x60;
    uint16_t unknown_0x64;
    uint8_t unknown_0x66[0x2];
    int32_t unknown_0x68;
    int32_t unknown_0x6c;
    uint64_t unknown_0x70;
    int32_t unknown_0x78;
    int32_t unknown_0x7c;
    uint8_t has_hit;
};
static_assert(offsetof(ClosestRayHit, unknown_0x10) == 0x10 && offsetof(ClosestRayHit, hit_fraction) == 0x40);
static_assert(offsetof(ClosestRayHit, unknown_0x48) == 0x48 && offsetof(ClosestRayHit, unknown_0x50) == 0x50);
static_assert(offsetof(ClosestRayHit, unknown_0x60) == 0x60 && offsetof(ClosestRayHit, unknown_0x70) == 0x70);
static_assert(offsetof(ClosestRayHit, has_hit) == 0x80);

RT_GLOBAL(0x0578d4e0, closest_ray_hit_vtable, const uint8_t);
RT_GLOBAL(0x02fd5470, ray_hit_start_vector, const Vec4);   // the collector's +0x10 starts as this
constexpr uint32_t largest_fraction_bits = 0x7f7fffee;    // just under the largest float
constexpr int32_t largest_int = 0x7fffffff;
constexpr int32_t none = -1;
constexpr uint16_t none_16 = 0xffff;

// The world's ray cast ("TtWorldCastRay"): fills `hit` with the closest hit along the ray.
RT_ORIGINAL(0x00f212a0, physics_world_cast_ray, void(PhysicsWorld *, const WorldRayInput *, ClosestRayHit *));

// The ray's reciprocal per lane, refined once (Newton); a zero lane gets the largest float with
// the lane's sign. `direction` must have no negative zeros (the caller clears them).
constexpr float newton_two = 2.0f;
constexpr uint32_t sign_bits = 0x80000000;
constexpr uint32_t xyz_lane_bits = 0x7;
constexpr uint32_t lane_mask_float_bits = 0x3f000000;

Vec4 ray_inverse_direction(Vec4 direction)
{
    const Vec4 zero = Vec4::zero();
    const Vec4 is_zero = vec::equal(direction, zero);
    const Vec4 estimate = engine::reciprocal_estimate(direction);
    const Vec4 refined =
        vec::multiply(estimate, vec::subtract(vec::broadcast(newton_two), vec::multiply(estimate, direction)));
    const Vec4 signed_largest = vec::bit_xor(vec::bit_and(direction, vec::broadcast(std::bit_cast<float>(sign_bits))),
                                             vec::broadcast(std::bit_cast<float>(largest_fraction_bits)));
    const Vec4 inverse = vec::select(refined, signed_largest, is_zero);
    const uint32_t not_negative = engine::less_equal_lanes(zero, direction);
    const float lane_mask = std::bit_cast<float>((not_negative & xyz_lane_bits) | lane_mask_float_bits);
    return vec::lanes(inverse.x(), inverse.y(), inverse.z(), lane_mask);
}

// A vector with only its y lane set: a height.
Vec4 height_only(float height) { return vec::lanes(0.0f, height, 0.0f, 0.0f); }

// Is the line from the observer's eye to the target's clear? The ray is set up as the original
// does: the end point rebuilt from the start plus the difference, its w lane 1, negative zeros
// cleared.
bool line_of_sight_clear(PhysicsWorldHolder *holder, const SightQuery *query)
{
    PhysicsWorld *world = holder->world;
    const Vec4 origin = vec::add_in_order(height_only(query->observer_eye_height), query->observer_position);
    const Vec4 end = vec::add_in_order(height_only(query->target_eye_height), query->target_position);
    const Vec4 rebuilt_end = vec::add_in_order(origin, vec::subtract(end, origin));
    Vec4 direction = vec::subtract(rebuilt_end, origin);
    direction = vec::lanes(direction.x(), direction.y(), direction.z(), 1.0f);
    direction = vec::bit_and_not(vec::equal(direction, Vec4::zero()), direction);

    WorldRayInput input;
    input.filter = world->filter_owner->ray_filter;
    input.filter_group = any_filter_group;
    input.collision_layer = sight_collision_layer;
    input.unknown_0x10 = 0;
    input.unknown_0x18 = 0;
    input.origin = origin;
    input.direction = direction;
    input.inverse_direction = ray_inverse_direction(direction);
    input.unknown_0x50 = 0;

    ClosestRayHit hit;
    hit.vtable = closest_ray_hit_vtable.address();
    hit.unknown_0x08 = 0;
    hit.unknown_0x10 = ray_hit_start_vector.get();
    hit.unknown_0x20 = Vec4::zero();
    hit.unknown_0x30 = Vec4::zero();
    hit.hit_fraction = std::bit_cast<float>(largest_fraction_bits);
    hit.unknown_0x48 = largest_int;
    hit.unknown_0x4c = none_16;
    hit.unknown_0x50 = none;
    hit.unknown_0x54 = 0;
    hit.unknown_0x58 = 0;
    hit.unknown_0x60 = largest_int;
    hit.unknown_0x64 = none_16;
    hit.unknown_0x68 = none;
    hit.unknown_0x6c = 0;
    hit.unknown_0x70 = 0;
    hit.unknown_0x78 = 0;
    hit.unknown_0x7c = 0;
    hit.has_hit = 0;
    physics_world_cast_ray(holder->world, &input, &hit);
    return !hit.has_hit;
}

// ---- the tests ----

// Each sight distance from the row: whole metres, 0 without a row.
float metres(const ThinkParamSight *row, uint16_t ThinkParamSight::*field)
{
    return row ? static_cast<float>(row->*field) : 0.0f;
}
float radians(const ThinkParamSight *row, uint8_t ThinkParamSight::*field)
{
    return row ? engine::multiply(static_cast<float>(row->*field), degrees_to_radians) : 0.0f;
}

uint16_t ThinkParamSight::*eye_distance_field(uint8_t darkness)
{
    if (darkness == darkness_pitch_dark) return &ThinkParamSight::eye_dist_for_pitch_dark;
    if (darkness == darkness_dark) return &ThinkParamSight::eye_dist_for_dark;
    return &ThinkParamSight::eye_dist;
}

// A unit vector along `value` (lanes as the original computes them).
Vec4 unit(Vec4 value)
{
    const Vec4 squared = squared_length4(value);
    return normalized(value, squared, refined_length(squared));
}

// The eye's three geometric tests: distance (from the moved-back eye point), width angle on the
// ground, height angle. The ray is checked by the caller.
bool inside_eye_range(const SightQuery *query, const ThinkParamSight *row)
{
    const float scale = query->target_visibility, own_scale = query->observer_sight_scale;
    auto scaled = [&](float distance) { return engine::multiply(engine::multiply(distance, scale), own_scale); };
    const float eye_reach =
        engine::add(scaled(metres(row, eye_distance_field(query->target_darkness))), query->observer_size);
    const float begin = scaled(metres(row, &ThinkParamSight::eye_begin_dist));
    const float width_angle = radians(row, &ThinkParamSight::eye_ang_y);
    const float height_angle = radians(row, &ThinkParamSight::eye_ang_x);
    const float back_offset = scaled(metres(row, &ThinkParamSight::eye_back_offset_dist));

    const Vec4 flat_facing = flattened(query->observer_facing);
    const Vec4 eye_point =
        vec::subtract(query->observer_position, vec::multiply(vec::broadcast(back_offset), flat_facing));
    const Vec4 to_target = vec::subtract(query->target_position, eye_point);
    const float distance_squared = dot3(to_target, to_target);
    if (!(distance_squared >= engine::multiply(begin, begin))) return false;
    const float reach = engine::add(eye_reach, query->target_size);
    if (!(engine::multiply(reach, reach) > distance_squared)) return false;

    // Width: the angle between the facing and the line to the target, both laid flat.
    const Vec4 flat_line = flattened(to_target);
    const float width_cosine = cosine(wrapped_once(width_angle, true));
    if (!(dot3(unit(flat_line), unit(flat_facing)) >= width_cosine)) return false;

    // Height: the cosine of the line's elevation, from (0, rise, ground distance).
    const float ground_distance = refined_length(squared_length3(flat_line)).x();
    const Vec4 elevation = unit(vec::lanes(0.0f, to_target.y(), ground_distance, 0.0f));
    const float height_cosine = cosine(wrapped_once(height_angle, true));
    return dot3(elevation, vec::lanes(0.0f, 0.0f, 1.0f, 0.0f)) >= height_cosine;
}

bool within_nose_distance(const SightQuery *query, const ThinkParamSight *row)
{
    const float reach =
        engine::add(engine::add(metres(row, &ThinkParamSight::nose_dist), query->observer_size), query->target_size);
    const Vec4 to_target = vec::subtract(query->target_position, query->observer_position);
    return engine::multiply(reach, reach) > dot3(to_target, to_target);
}

SightResult sight_result(const SightQuery *query, bool check_line_of_sight)
{
    const ThinkParamSight *row = query->think_params->row;
    if (within_nose_distance(query, row)) return SightResult::smelled;
    if (!inside_eye_range(query, row)) return SightResult::not_seen;
    if (!check_line_of_sight) return SightResult::seen;
    PhysicsWorldHolder *holder = physics_manager_instance.get()->holder;
    if (!holder->world || line_of_sight_clear(holder, query)) return SightResult::seen;
    return SightResult::not_seen;
}

}  // namespace
}  // namespace ai

using namespace ai;

// The sight test: stores the answer in the query and returns whether the target was noticed
// (by nose or eye). A target marked unseeable is never noticed, though the test still runs (and
// casts its ray).
extern "C" bool bb_ai_sight_test(SightQuery *query, bool check_line_of_sight)
{
    SightResult result = sight_result(query, check_line_of_sight);
    if (query->target_unseeable) result = SightResult::not_seen;
    query->result = result;
    return result != SightResult::not_seen;
}

// Resets the target half of a sight query: at the origin, no height or size, visibility 1, in the
// light, seeable.
extern "C" void bb_ai_sight_query_reset_target(SightQuery *query)
{
    query->target_position = vec::lanes(0.0f, 0.0f, 0.0f, 1.0f);
    query->target_eye_height = 0.0f;
    query->target_size = 0.0f;
    query->target_visibility = 1.0f;
    query->target_darkness = 0;
    query->target_unseeable = 0;
}

namespace ai {
namespace {

// ---- the target list: adding or refreshing what was noticed ----

// A noticed character, as the refresh reads it: its id.
struct NoticedCharacter {
    uint8_t unknown_0x00[0x8];
    int32_t id;
};
static_assert(offsetof(NoticedCharacter, id) == 0x8);

// Whatever caused the notice, when the caller passes one: an id through its vtable.
struct NoticeSource;
struct NoticeSourceVtable {
    void *unknown_slots[0x88 / 8];
    int32_t (*id)(NoticeSource *);
};
static_assert(offsetof(NoticeSourceVtable, id) == 0x88);
struct NoticeSource {
    const NoticeSourceVtable *vtable;
};

// The whole 0xa0-byte target-list node as the refresh builds a new one. TargetNode
// (ai_character.h) names the fields the other target-list functions read; this view adds the
// ones only the constructor sets. From +0x20 on the node holds the entry's target record, which
// the notice update below takes.
struct NewTargetNode {
    uint16_t unknown_0x00;
    uint8_t node_kind;
    uint8_t unknown_0x03[0x5];
    NewTargetNode *next;
    NewTargetNode *previous;
    uint8_t unknown_0x18[0x8];
    int32_t record_type;
    int32_t unknown_0x24;
    TargetKind kind;
    int32_t refresh_clock;
    float priority;
    float recheck_timer;
    int32_t unknown_0x38;
    float unknown_0x3c;
    uint8_t visible;
    uint8_t matches_main;
    uint8_t unknown_0x42;
    uint8_t unknown_0x43[0x5];
    const void *handle_vtable;
    int32_t handle_id;
    uint8_t unknown_0x54[0x4];
    uint64_t unknown_0x58;
    const void *place_handle_vtable;
    uint8_t unknown_0x68[0x8];
    Vec4 place;
    uint8_t place_valid;
    uint8_t unknown_0x81[0x3];
    int32_t unknown_0x84;
    uint8_t unknown_0x88[0x8];
    NoticeSource *source;
    uint8_t unknown_0x98[0x8];
};
static_assert(sizeof(NewTargetNode) == 0xa0 && offsetof(NewTargetNode, record_type) == 0x20);
static_assert(offsetof(NewTargetNode, kind) == offsetof(TargetNode, kind));
static_assert(offsetof(NewTargetNode, refresh_clock) == 0x2c && offsetof(NewTargetNode, priority) == 0x30);
static_assert(offsetof(NewTargetNode, visible) == offsetof(TargetNode, visible));
static_assert(offsetof(NewTargetNode, handle_vtable) == offsetof(TargetNode, handle));
static_assert(offsetof(NewTargetNode, place_handle_vtable) == offsetof(TargetNode, place_handle));
static_assert(offsetof(NewTargetNode, place) == offsetof(TargetNode, place) && offsetof(NewTargetNode, source) == 0x90);

// The refresh clock is a word at +0x2c that the refresh clears (what counts it is not known yet).
struct TargetNodeRefreshClock {
    uint8_t unknown_0x00[0x2c];
    int32_t refresh_clock;
};

constexpr uint8_t node_refreshed = 0x80;   // set on the entries this update noticed
constexpr int32_t new_record_type = 2;
constexpr float new_unknown_0x3c = 1.0f;
constexpr uint64_t target_node_size = sizeof(NewTargetNode);
constexpr uint64_t target_node_alignment = 0x10;
constexpr uint64_t target_record_offset = offsetof(NewTargetNode, record_type);

RT_GLOBAL(0x05940420, allocator_instance, engine::Allocator *);
RT_GLOBAL(0x05765d40, character_handle_vtable, const uint8_t);
RT_GLOBAL(0x0571b6c0, place_handle_class, const uint8_t);   // the vtable starts 0x10 bytes in
constexpr uint32_t vtable_offset_in_class = 0x10;

// Updates the entry's target record for this notice: `refreshed` says whether the entry was
// already there; `sense` picks how (0..6).
RT_ORIGINAL(0x016b2af0, target_record_notice, void(TargetList *, void *record, bool refreshed, int32_t sense, int32_t id));
// Passes a newly noticed character on, with the source's id (what receives it is not known yet).
RT_ORIGINAL(0x023f36f0, report_new_notice, void(int32_t source_id, NoticedCharacter *));

void *target_record(void *node) { return static_cast<uint8_t *>(node) + target_record_offset; }

// The target handle of a character or place entry (null for other kinds, which never get here).
TargetHandle *entry_handle(TargetNode *node)
{
    if (node->kind == TargetKind::place) return &node->place_handle;
    if (node->kind == TargetKind::character) return &node->handle;
    return nullptr;
}

// The entry, character or place, whose handle has this id. No entry has id -1.
TargetNode *entry_with_id(TargetList *list, int32_t id)
{
    for (TargetNode *node = list->first; node;) {
        TargetNode *next = node->next;
        const bool entry = (node->node_kind & node_kind_mask) == target_entry_node &&
                           (node->kind == TargetKind::character || node->kind == TargetKind::place);
        if (entry) {
            TargetHandle *handle = entry_handle(node);
            if (id != no_target_id && handle->vtable->id(handle) == id) return node;
        }
        node = next;
    }
    return nullptr;
}

NewTargetNode *new_entry(TargetList *list, NoticeSource *source)
{
    auto *node = static_cast<NewTargetNode *>(allocator_instance.get()->allocate(target_node_size, target_node_alignment));
    if (!node) return nullptr;
    node->unknown_0x00 = 0;
    node->node_kind = 0;
    node->next = nullptr;
    node->handle_vtable = character_handle_vtable.address();
    node->place_handle_vtable = place_handle_class.address() + vtable_offset_in_class;
    node->unknown_0x84 = none;
    node->recheck_timer = 0.0f;
    node->unknown_0x38 = 0;
    node->refresh_clock = 0;
    node->priority = 0.0f;
    node->unknown_0x3c = new_unknown_0x3c;
    node->kind = static_cast<TargetKind>(0);
    node->visible = 0;
    node->matches_main = 0;
    node->unknown_0x42 = 1;
    node->unknown_0x24 = 0;
    node->handle_id = none;
    node->unknown_0x58 = 0;
    node->place = vec::lanes(0.0f, 0.0f, 0.0f, 1.0f);
    node->place_valid = 0;
    node->record_type = new_record_type;

    // Linked in first.
    auto *first = reinterpret_cast<NewTargetNode *>(list->first);
    node->next = first;
    node->previous = nullptr;
    if (first) first->previous = node;
    list->first = reinterpret_cast<TargetNode *>(node);
    node->unknown_0x00 = 0;
    node->source = source;
    node->node_kind = target_entry_node | node_refreshed;
    return node;
}

}  // namespace
}  // namespace ai

// A character was noticed (by `sense`): refresh its entry in the target list (refresh clock 0,
// visible, marked as noticed this update), or add a new entry for it, and pass a new notice on
// to whoever the source names. False without a character or when no node could be allocated.
extern "C" bool bb_ai_target_list_refresh_noticed(TargetList *list, NoticedCharacter *character, int32_t sense,
                                                  NoticeSource *source)
{
    if (!character) return false;
    if (TargetNode *entry = entry_with_id(list, character->id)) {
        reinterpret_cast<TargetNodeRefreshClock *>(entry)->refresh_clock = 0;
        entry->visible = 1;
        entry->node_kind |= node_refreshed;
        target_record_notice(list, target_record(entry), true, sense, character->id);
        return true;
    }
    NewTargetNode *node = new_entry(list, source);
    if (!node) return false;
    target_record_notice(list, target_record(node), false, sense, character->id);
    if (source) report_new_notice(source->vtable->id(source), character);
    return true;
}
