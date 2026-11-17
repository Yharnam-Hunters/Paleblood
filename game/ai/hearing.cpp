// SPDX-License-Identifier: GPL-2.0-or-later
// AI: hearing. For each sound around an enemy, the sound check (0x0169cef0) finds who or what
// made it and whether the enemy can hear that kind of maker; the hearing test (0x0169d160) then
// decides whether the enemy hears the sound and, when it does, remembers where it came from; the
// ear's shape (0x0169da30) answers whether a sphere around a point reaches the enemy's ear.
//
// The sound check, in plain words:
// - A sound made by a character is checked by teams. Each character belongs to a team (a number
//   below 32), and two 32 x 32 tables of the game say which teams hear which: one for most
//   sounds, and, for sounds whose AiSound row sets useHitDamageTeam, the pair of attack-hit team
//   tables, each counting only when the sound asks for it. The maker's size goes to the hearing
//   test, which takes it off the gap. A sound whose character is gone is not heard.
// - A sound made by an object is heard when the object still exists, and a sound of the third
//   kind always; neither has a size. A sound of any other kind is not heard.
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
//   not farther than this sound's gap) and, always, its AiSound id.
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
#include "../param/param_repository.h"
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
struct Team;
struct HearingOwnerVtable {
    void *unknown_slots_0x00[0x10 / 8];
    void (*team)(Team *out, HearingOwner *);
    Vec4 (*position)(HearingOwner *);
    void *unknown_slot_0x20;
    Vec4 (*forward)(HearingOwner *);
    void *unknown_slots_0x30[(0x40 - 0x30) / 8];
    float (*size)(HearingOwner *);
};
static_assert(offsetof(HearingOwnerVtable, team) == 0x10);
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

// Who or what made a sound.
enum class SoundMaker : int32_t {
    character = 0,
    object = 1,
    other = 2,                    // heard without asking who made it
};

// One sound, as the sound check and the hearing test read it.
struct Sound {
    uint8_t unknown_0x00[0x10];
    Vec4 position;
    SoundMaker maker;
    uint32_t maker_handle;        // the character or object that made it; no_handle for none
    float range;
    uint8_t unknown_0x2c[0x4];
    int32_t ai_sound_id;          // the sound's AiSound param row
    uint8_t uses_hit_team_table_a;  // with useHitDamageTeam: the first attack-hit table counts
    uint8_t uses_hit_team_table_b;  // with useHitDamageTeam: the second one counts
};
static_assert(offsetof(Sound, position) == 0x10 && offsetof(Sound, maker) == 0x20);
static_assert(offsetof(Sound, maker_handle) == 0x24 && offsetof(Sound, range) == 0x28);
static_assert(offsetof(Sound, ai_sound_id) == 0x30 && offsetof(Sound, uses_hit_team_table_a) == 0x34);
static_assert(offsetof(Sound, uses_hit_team_table_b) == 0x35);

constexpr float at_the_ear = 0.0001f;          // closer than this is always heard
constexpr float flat_length_epsilon = 0.00001f;  // a shorter ground direction is no direction
// pi / 2: the height angle without a row, and the least ear_angX that hears at every height.
constexpr uint32_t right_angle_bits = 0x3fc90fdb;

float length3(Vec4 value) { return refined_length(squared_length3(value)).x(); }

float radians(uint8_t degrees) { return engine::multiply(static_cast<float>(degrees), degrees_to_radians); }

float ear_height_angle(const Listener *listener)
{
    const ThinkParamHearing *row = listener->think_params->row;
    if (!row) return std::bit_cast<float>(right_angle_bits);
    return radians(row->ear_ang_x);
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

// True when the sphere of `radius` around `point` reaches the ear at *listener_position. The
// hearing test below is its only caller.
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
constexpr float one_side = 1.0f, other_side = -1.0f;

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
    const bool every_height = !(radians(row->ear_ang_x) < std::bit_cast<float>(right_angle_bits));
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

// Remembers a heard sound: its position, unless a remembered position is not farther than this
// sound's gap (a tie keeps the old position); always its AiSound id.
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
// heard from where the enemy is: `audible`), remembering it when it does. The sound check below
// is its only caller. `source_size` is the
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

namespace {

// ---- the sound check: who made the sound ----

constexpr uint32_t no_handle = 0xffffffff;

// The world's characters (WorldChrMan), as a handle finds one: bits 14..19 of the handle pick a
// character set (ChrSet) and the low 14 bits an entry in it (ChrSetEntry, 0x38 bytes). The
// layout names follow droogie/bbhost's notes (world_chr_man.hpp, chr_handle.hpp, chr_set.hpp).
// A sound's maker, as far as the sound check reads it: its size sits in the physics module
// (ChrIns +0x3b0 module table, +0x68; bbhost's SprjChrPhysicsModule).
struct MakerPhysics {
    uint8_t unknown_0x000[0x358];
    float size;                   // taken off the gap by the hearing test
};
struct MakerModules {
    uint8_t unknown_0x00[0x68];
    const MakerPhysics *physics;
};
struct MakerCharacter {
    uint8_t unknown_0x000[0x3b0];
    const MakerModules *modules;
};
static_assert(offsetof(MakerPhysics, size) == 0x358 && offsetof(MakerModules, physics) == 0x68);
static_assert(offsetof(MakerCharacter, modules) == 0x3b0);

struct CharacterSlot {
    MakerCharacter *character;
    uint8_t unknown_0x08[0x30];
};
static_assert(sizeof(CharacterSlot) == 0x38);
struct CharacterBlock {
    int32_t count;
    uint8_t unknown_0x04[0x4];
    CharacterSlot *slots;
};
static_assert(offsetof(CharacterBlock, slots) == 0x8);
struct CharacterBlockLimit {
    uint8_t unknown_0x00[0x18];
    int32_t extra_blocks;         // the blocks a handle may name, less uncounted_blocks
};
constexpr int32_t uncounted_blocks = 2;
static_assert(offsetof(CharacterBlockLimit, extra_blocks) == 0x18);
constexpr int character_block_shift = 14, character_block_bits = 6;
constexpr uint32_t character_slot_mask = 0x3fff;
struct WorldCharacters {
    uint8_t unknown_0x000[0x8];
    const CharacterBlockLimit *limit;
    uint8_t unknown_0x010[0x840];
    CharacterBlock *blocks[1 << character_block_bits];
};
static_assert(offsetof(WorldCharacters, limit) == 0x8 && offsetof(WorldCharacters, blocks) == 0x850);

RT_GLOBAL(0x0593e878, world_characters, WorldCharacters *);
RT_GLOBAL(0x04d3b26b, world_characters_name, const char);

// The world's objects, as a handle finds one: bits 20..27 of the handle pick a group and the low
// 20 bits a slot in it. Only whether the slot exists matters here.
struct ObjectSlot {
    uint8_t unknown_0x000[0x2e0];
};
struct ObjectGroup {
    uint8_t unknown_0x000[0xc8];
    int32_t count;
    uint8_t unknown_0x0cc[0x4];
    ObjectSlot *slots;
    uint8_t unknown_0x0d8[0x120];
};
static_assert(offsetof(ObjectGroup, count) == 0xc8 && offsetof(ObjectGroup, slots) == 0xd0);
static_assert(sizeof(ObjectGroup) == 0x1f8);
struct ObjectTable {
    uint8_t unknown_0x00[0x20];
    int32_t group_count;
    uint8_t unknown_0x24[0x4];
    ObjectGroup *groups;
};
static_assert(offsetof(ObjectTable, group_count) == 0x20 && offsetof(ObjectTable, groups) == 0x28);
constexpr int object_group_shift = 20, object_group_bits = 8;
constexpr uint32_t object_slot_mask = 0xfffff;

RT_GLOBAL(0x0593b168, world_objects, ObjectTable *);

uint32_t bits(uint32_t value, int shift, int count) { return (value >> shift) & ((1u << count) - 1); }

// Whether `slot` of an array at `slots` is a real address: the game checks the slot's address,
// not the array's, so a null array still has its slots past the first.
template <typename Slot> bool slot_address_set(const Slot *slots, uint32_t slot)
{
    return reinterpret_cast<uintptr_t>(slots) + slot * sizeof(Slot) != 0;
}

// The character a handle names, or null when it names none.
MakerCharacter *find_character(const WorldCharacters *world, uint32_t handle)
{
    if (handle == no_handle) return nullptr;
    const uint32_t block_index = bits(handle, character_block_shift, character_block_bits);
    if (static_cast<int32_t>(block_index) >= world->limit->extra_blocks + uncounted_blocks) return nullptr;
    const CharacterBlock *block = world->blocks[block_index];
    if (!block) return nullptr;
    const uint32_t slot = handle & character_slot_mask;
    if (static_cast<int32_t>(slot) >= block->count || !slot_address_set(block->slots, slot)) return nullptr;
    return block->slots[slot].character;
}

// Whether the object a handle names still exists.
bool object_exists(const ObjectTable *objects, uint32_t handle)
{
    if (handle == no_handle) return false;
    const uint32_t group_index = bits(handle, object_group_shift, object_group_bits);
    if (static_cast<int32_t>(group_index) >= objects->group_count) return false;
    const ObjectGroup &group = objects->groups[group_index];
    const uint32_t slot = handle & object_slot_mask;
    return static_cast<int32_t>(slot) < group.count && slot_address_set(group.slots, slot);
}

// ---- the sound check: whether the listener's team hears the maker's ----

// What the team queries fill in: a small object whose `number` is the team type (below 32). For
// a character, 0x01cc2910 starts from its ChrIns team_type (+0x88) and may override it from its
// effects (one override is 29, bbhost's Indiscriminate team); it is called, not rewritten here.
struct Team {
    const void *vtable;
    uint32_t number;
};
static_assert(offsetof(Team, number) == 0x8);
constexpr uint32_t team_count = 32;
using TeamTable = uint8_t[team_count][team_count];   // [listener's team][maker's team]: nonzero hears

RT_ORIGINAL(0x01cc2910, character_team, void(Team *out, const MakerCharacter *));
// The table for most sounds (the global holds its address) and the two attack-hit tables.
RT_GLOBAL(0x057e4560, hearing_team_table, const TeamTable *);
RT_GLOBAL(0x04b31560, hit_team_table_a, const TeamTable);
RT_GLOBAL(0x04b31960, hit_team_table_b, const TeamTable);

// The part of an AiSoundParam row the sound check reads.
struct AiSoundRow {
    uint8_t unknown_0x00[0xc];
    uint8_t use_hit_damage_team;  // judge by the attack-hit team tables
};
static_assert(offsetof(AiSoundRow, use_hit_damage_team) == 0xc);
RT_ORIGINAL(0x02312b60, find_ai_sound_row, void(param::RowLookup *out, int32_t id));

RT_ORIGINAL(0x0169d160, hearing_test, void(Listener *, const Sound *, bool audible, float source_size));

// Whether the listener's team hears sounds `maker` makes.
bool team_hears(const Listener *listener, const Sound *sound, const MakerCharacter *maker)
{
    param::RowLookup lookup{sound->ai_sound_id, {}, nullptr};
    find_ai_sound_row(&lookup, sound->ai_sound_id);
    const auto *row = reinterpret_cast<const AiSoundRow *>(lookup.row);

    Team listener_team, maker_team;
    HearingOwner *owner = listener->owner;
    owner->vtable->team(&listener_team, owner);
    character_team(&maker_team, maker);
    const uint32_t l = listener_team.number, m = maker_team.number;
    if (!row || !row->use_hit_damage_team) return (*hearing_team_table.get())[l][m] != 0;
    return (sound->uses_hit_team_table_a && hit_team_table_a.get()[l][m]) ||
           (sound->uses_hit_team_table_b && hit_team_table_b.get()[l][m]);
}

}  // namespace

// The sound check: finds who or what made `sound` and hands it to the hearing test with whether
// the enemy can hear that maker and the maker's size.
extern "C" void bb_ai_hearing_check_sound(Listener *listener, const Sound *sound)
{
    switch (sound->maker) {
    case SoundMaker::other:
        hearing_test(listener, sound, true, 0.0f);
        return;
    case SoundMaker::object:
        if (object_exists(world_objects.get(), sound->maker_handle)) hearing_test(listener, sound, true, 0.0f);
        return;
    case SoundMaker::character: {
        const WorldCharacters *world = engine::require(world_characters, world_characters_name.address());
        const MakerCharacter *maker = find_character(world, sound->maker_handle);
        if (!maker) return;
        const float maker_size = maker->modules->physics->size;
        hearing_test(listener, sound, team_hears(listener, sound, maker), maker_size);
        return;
    }
    }
}

}  // namespace ai
