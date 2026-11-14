// SPDX-License-Identifier: GPL-2.0-or-later
// AI: perception. What an enemy's AI scripts ask about seeing and placing their target, and the
// record of how fast the target moves that every AI character keeps each frame.
//
// - IsVisibleTarget does not look: it reads the answer the perception update left in the target
//   list ("visible" on the target the character is following), except for selectors that name a
//   fixed point, which always count as visible.
// - IsInsideTargetEx is the geometric test: is the target closer than a distance, and within an
//   angle of a direction (the character's front, back or sides, or the line towards the target
//   and its back and sides)? The angle test runs in the horizontal plane: heights are ignored,
//   distance is not. The angle is the whole cone, so half of it lies on each side of the
//   direction.
// - The speed record keeps the last 30 speeds of the followed target along the line between the
//   two characters (positive: coming closer to the character), and their weighted average.
//
// Layouts are in ai_think.h and ai_character.h.
#include <bit>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "../engine/vector_operations.h"
#include "ai_think.h"
#include "perception_math.h"

namespace ai {

using engine::Vec4;
namespace vec = engine::vector_operations;
using namespace perception_math;

namespace {

// ---- the engine's character manager, as these functions read it ----

struct WorldCharacterManager {
    uint8_t unknown_0x00[0x60];
    Character *player;
};
static_assert(offsetof(WorldCharacterManager, player) == 0x60);

RT_GLOBAL(0x0593e878, world_character_manager, WorldCharacterManager *);
RT_GLOBAL(0x04d3b26b, world_character_manager_name, const char);
// The player's character-set entry, which these functions store each time they run (others read
// it).
RT_GLOBAL(0x05943ed0, player_set_entry, void *);

void remember_player_set_entry()
{
    WorldCharacterManager *manager =
        engine::require(world_character_manager, world_character_manager_name.address());
    player_set_entry.get() = manager->player->set_entry;
}

// ---- the target list ----

// Kinds of target-list node (the low seven bits of node_kind). Entries (kind 1) are what the
// character has noticed; the other kinds take precedence over them in the order below. What they
// stand for is not known yet; they are named by number.
constexpr uint8_t node_kind_3 = 3;
constexpr uint8_t node_kind_4 = 4;
constexpr uint8_t node_kind_5 = 5;
constexpr uint8_t node_kind_11 = 11;

TargetNode *first_node_of_kind(TargetList *list, uint8_t kind)
{
    for (TargetNode *node = list->first; node; node = node->next) {
        if ((node->node_kind & node_kind_mask) == kind) return node;
    }
    return nullptr;
}

// The entry with the highest priority above -1 (the first of equals), or null.
TargetNode *main_entry(TargetList *list)
{
    TargetNode *best = nullptr;
    float best_priority = no_priority;
    for (TargetNode *node = list->first; node; node = node->next) {
        if ((node->node_kind & node_kind_mask) == target_entry_node && node->priority > best_priority) {
            best = node;
            best_priority = node->priority;
        }
    }
    return best;
}

// The current target of the speed record: the perception's own choice, or else the first kind-11
// node's character or place.
RT_ORIGINAL(0x016ad530, perception_current_target, TargetHandle *(ThinkPerception *, int32_t mode));
constexpr int32_t current_target_default_mode = 0;

// Shuffles used only here, by what they produce.
constexpr int xyz_then_x = 0x24;          // (x, y, z, x)
constexpr int rotate_yzx = 0xc9;          // (y, z, x, w)
constexpr int side_order = 0x49;          // (y, z, x, y)

// ---- IsInsideTargetEx: within an angle of a direction, and within a distance ----

// Which way the test faces. A heading in radians from the character's front (0) round to its
// back (pi); headings in the target-relative variants are measured from the line towards the
// target instead. 100 and 1000 are markers for "no heading" (the front is used).
struct AiDirection {
    float heading;
    uint8_t unknown_4;
    uint8_t from_target_line;   // nonzero: measured from the line towards the target
    uint8_t unknown_6[2];
};
static_assert(sizeof(AiDirection) == 8 && offsetof(AiDirection, from_target_line) == 5);

// The bindings call the inside test and the visibility lookup (both replaced below) through their
// original addresses.
RT_ORIGINAL(0x01693850, ai_is_inside_direction, bool(AiCharacter *, AiCharacter *, AiDirection, float, float));
RT_ORIGINAL(0x016ad400, ai_perception_target_visible, uint8_t(ThinkPerception *));

constexpr float half_of_cone = 0.5f;   // the angle is the whole cone; each side gets half
constexpr float no_heading = 100.0f;
constexpr float no_heading_too = 1000.0f;

// The script's direction numbers (AI_DIR_TYPE): 0 none, 1-4 front, back and the two sides of the
// character, 5-8 the same around the line towards the target, 9 none.
constexpr uint32_t direction_count = 10;
constexpr int32_t first_target_line_direction = 5;
constexpr int32_t last_target_line_direction = 8;
RT_GLOBAL(0x04b30090, direction_headings, const float);   // one heading per direction number

RT_GLOBAL(0x04d2bb09, direction_check_file, const char);
RT_GLOBAL(0x04d2bb42, direction_check_message, const char);
constexpr int32_t direction_check_line = 0x4f;

// Each heading falls in one quarter, which picks the direction vector to test against.
enum class Quarter : int32_t {
    front = 1,
    back = 2,
    minus_side = 3,
    plus_side = 4,
    target_line = 5,
    away_from_target_line = 6,
    target_line_minus_side = 7,
    target_line_plus_side = 8,
};
struct HeadingRange {
    float from, to;
    Quarter own, along_target_line;
};
constexpr float quarter_turn = 0.7853982f;        // pi / 4
constexpr float three_quarter_turn = 2.3561945f;  // 3 pi / 4
constexpr float half_turn = 3.1415927f;           // pi
constexpr HeadingRange heading_ranges[] = {
    {-quarter_turn, quarter_turn, Quarter::front, Quarter::target_line},
    {three_quarter_turn, half_turn, Quarter::back, Quarter::away_from_target_line},
    {-half_turn, -three_quarter_turn, Quarter::back, Quarter::away_from_target_line},
    {-three_quarter_turn, -quarter_turn, Quarter::minus_side, Quarter::target_line_minus_side},
    {quarter_turn, three_quarter_turn, Quarter::plus_side, Quarter::target_line_plus_side},
};

Quarter quarter_of(AiDirection direction)
{
    const float heading = direction.heading;
    if (heading == no_heading || heading == no_heading_too) return Quarter::front;
    for (const HeadingRange &range : heading_ranges) {
        if (heading >= range.from && range.to >= heading)
            return direction.from_target_line != 0 ? range.along_target_line : range.own;
    }
    return Quarter::front;
}

// A line at right angles to `line` on the ground, built as the original does from the line's
// x and z lanes (the two variants are opposite).
Vec4 target_line_side(Vec4 line, bool minus)
{
    const Vec4 z_part = vec::shuffle_words<rotate_yzx>(vec::multiply_in_order(line, vec::lanes(0.0f, 0.0f, 1.0f, 0.0f)));
    const Vec4 x_part = vec::multiply_in_order(line, Vec4::only_x(1.0f));
    const Vec4 difference = minus ? vec::subtract(z_part, x_part) : vec::subtract(x_part, z_part);
    return vec::shuffle_words<side_order>(difference);
}

Vec4 direction_vector(AiCharacter *self, Quarter quarter, Vec4 to_target)
{
    switch (quarter) {
    case Quarter::back: return negated(self->vtable->forward(self));
    case Quarter::minus_side: return negated(self->vtable->side(self));
    case Quarter::plus_side: return self->vtable->side(self);
    case Quarter::target_line: return to_target;
    case Quarter::away_from_target_line: return negated(to_target);
    case Quarter::target_line_minus_side: return target_line_side(to_target, true);
    case Quarter::target_line_plus_side: return target_line_side(to_target, false);
    case Quarter::front: break;
    }
    return self->vtable->forward(self);
}

}  // namespace
}  // namespace ai

using namespace ai;

// Is `target` within `distance` of `self` (straight-line, heights included), and within the cone
// of `angle` degrees around `direction` on the ground? False when either character is missing.
extern "C" bool bb_ai_is_inside_direction(AiCharacter *self, AiCharacter *target, AiDirection direction,
                                          float angle, float distance)
{
    if (!self || !target) return false;
    const Vec4 to_target = vec::subtract(target->vtable->position(target), self->vtable->position(self));
    if (!(distance >= refined_length(squared_length3(to_target)).x())) return false;

    const Vec4 facing = flattened(direction_vector(self, quarter_of(direction), to_target));
    const Vec4 facing_squared = squared_length4(facing);
    const Vec4 facing_unit = normalized(facing, facing_squared, refined_length(facing_squared));
    const Vec4 line = flattened(to_target);
    const Vec4 line_squared = squared_length4(line);
    const Vec4 line_unit = normalized(line, line_squared, refined_length(line_squared));

    const float half_angle = engine::multiply(engine::multiply(angle, half_of_cone), degrees_to_radians);
    const float wrapped = wrapped_once(wrapped_once(half_angle, false), true);
    return dot3(facing_unit, line_unit) > cosine(wrapped);
}

// IsInsideTargetEx(self, target, direction, angle, distance): the test above for the characters
// the two selectors name. A direction number out of range is reported, then used anyway (its
// heading is read past the table, as in the original).
extern "C" bool bb_ai_func_is_inside_target_ex(AiFunc *func, int32_t self_selector, int32_t target_selector,
                                               uint32_t direction, float angle, float distance)
{
    AiCharacter *self = ai_find_character(func->think, self_selector);
    AiCharacter *target = ai_find_character(func->think, target_selector);
    if (direction >= direction_count) {
        engine::fatal_error(direction_check_file.address(), direction_check_line, direction_check_message.address(),
                            direction);
    }
    const int32_t number = static_cast<int32_t>(direction);
    const AiDirection packed{(&direction_headings.get())[direction], 0,
                             number >= first_target_line_direction && number <= last_target_line_direction, {0, 0}};
    return ai_is_inside_direction(self, target, packed, angle, distance);
}

namespace {

// Selectors that always count as visible: -2, -1, 20, 21, 22, 24 and 25, and the points 100-123.
constexpr int32_t first_point_selector = 100;
constexpr uint32_t point_selector_count = 24;
constexpr int32_t always_visible_selectors[] = {-2, -1, 20, 21, 22, 24, 25};
bool always_visible(int32_t selector)
{
    for (int32_t fixed : always_visible_selectors) {
        if (selector == fixed) return true;
    }
    return static_cast<uint32_t>(selector) - static_cast<uint32_t>(first_point_selector) < point_selector_count;
}

}  // namespace

// Whether the character can see its current target: a kind-4 node's own answer, else true for a
// kind-5 node, else the main entry's, else true for kind-3 or kind-11 nodes, else false. With
// entries but none above priority -1 the original reads address 0x20 (it would crash). The flag's
// byte is returned as it is (not reduced to 0 or 1), and the bindings pass it on.
extern "C" uint8_t bb_ai_perception_target_visible(ThinkPerception *perception)
{
    TargetList *list = &perception->targets;
    if (TargetNode *node = first_node_of_kind(list, node_kind_4)) return node->visible;
    if (first_node_of_kind(list, node_kind_5)) return true;
    if (first_node_of_kind(list, target_entry_node)) return main_entry(list)->visible;
    return first_node_of_kind(list, node_kind_3) || first_node_of_kind(list, node_kind_11);
}

// IsVisibleTarget(selector). The selector only decides the always-visible cases; any other asks
// about the current target.
extern "C" uint8_t bb_ai_func_is_visible_target(AiFunc *func, int32_t selector)
{
    AiThink *think = func->think;
    remember_player_set_entry();
    if (always_visible(selector)) return true;
    return ai_perception_target_visible(think->perception);
}

namespace {

// The speed record of the target a character follows (one per think, updated every frame).
constexpr int32_t speed_sample_count = 30;
struct TargetSpeedRecord {
    AiThink *think;
    float average;                        // weighted, newest first (see the update)
    float samples[speed_sample_count];    // metres per second along the line between the two
    uint8_t unknown_0x084[0x168];
    int32_t next_sample;                  // where the next sample goes
    bool full;                            // all samples written at least once
    uint8_t unknown_0x1f1[0x3];
    int32_t target_id;                    // whose speed this is; another target starts over
    uint8_t unknown_0x1f8[0x8];
    Vec4 last_position;                   // the target's position last frame
};
static_assert(offsetof(TargetSpeedRecord, average) == 0x8 && offsetof(TargetSpeedRecord, samples) == 0xc);
static_assert(offsetof(TargetSpeedRecord, next_sample) == 0x1ec && offsetof(TargetSpeedRecord, full) == 0x1f0);
static_assert(offsetof(TargetSpeedRecord, target_id) == 0x1f4 && offsetof(TargetSpeedRecord, last_position) == 0x200);

TargetHandle *followed_target(ThinkPerception *perception)
{
    if (TargetHandle *handle = perception_current_target(perception, current_target_default_mode)) return handle;
    TargetNode *node = first_node_of_kind(&perception->targets, node_kind_11);
    if (!node) return nullptr;
    if (node->kind == TargetKind::place) return &node->place_handle;
    if (node->kind == TargetKind::character) return &node->handle;
    return nullptr;
}

// The target's speed since last frame along the line from it to the character: the movement
// projected on that line, its length, positive when the target comes closer to the character.
float speed_along_line(TargetSpeedRecord *record, TargetHandle *target, float frame_seconds)
{
    const Vec4 position = target->vtable->position(target);
    const Vec4 moved = vec::shuffle_words<xyz_then_x>(vec::subtract(position, record->last_position));
    ThinkSelf *self = record->think->self;
    const Vec4 line = vec::subtract(self->vtable->position(self), position);
    const Vec4 line_squared = squared_length4(line);
    const Vec4 line_unit = normalized(line, line_squared, refined_length(line_squared));
    const Vec4 along = vec::multiply_in_order(vec::shuffle_words<xyz_then_x>(line_unit), vec::broadcast(dot3(moved, line_unit)));
    const float length = refined_length(squared_length3(along)).x();
    const float sign = dot3(line_unit, along) > 0.0f ? 1.0f : -1.0f;
    return engine::divide(engine::multiply(length, sign), frame_seconds);
}

// The weight of the sample `age` frames old: 3 for the newest, falling by 1/15 per frame.
constexpr float weight_fall = -30.0f;
float sample_weight(int32_t age)
{
    const float fraction = engine::add(engine::divide(static_cast<float>(age), weight_fall), 1.0f);
    return engine::max_of(engine::add(engine::add(fraction, fraction), 1.0f), 0.0f);
}

// The weighted sum of the samples, newest first, divided by their number (not by the weights).
// Reading backwards from next_sample wraps from the first sample to the last; with next_sample 0
// the original first reads the float before the samples (the average, just set to 0).
void update_average(TargetSpeedRecord *record)
{
    const int32_t count = record->full ? speed_sample_count : record->next_sample;
    record->average = 0.0f;
    if (count <= 0) return;
    float sum = 0.0f;
    int32_t newer = record->next_sample;
    for (int32_t age = 0; age < count; age++) {
        sum = engine::add(sum, engine::multiply(sample_weight(age), (record->samples - 1)[newer]));
        record->average = sum;
        newer = newer <= 1 ? speed_sample_count : newer - 1;
    }
    record->average = engine::divide(sum, static_cast<float>(count));
}

}  // namespace

// Each frame: add the followed target's speed to the record (starting over when the target
// changed), remember where it is, and update the average. Without a perception or a target the
// record is emptied.
extern "C" void bb_ai_target_speed_record_update(TargetSpeedRecord *record, float frame_seconds)
{
    AiThink *think = record->think;
    remember_player_set_entry();
    ThinkPerception *perception = think->perception;
    TargetHandle *target = perception ? followed_target(perception) : nullptr;
    if (!target) {
        record->next_sample = 0;
        record->full = false;
        record->target_id = 0;
        update_average(record);
        return;
    }
    const int32_t id = target->vtable->id(target);
    if (id != record->target_id) {
        record->next_sample = 0;
        record->full = false;
        record->target_id = id;
    } else {
        record->samples[record->next_sample] = speed_along_line(record, target, frame_seconds);
        if (++record->next_sample >= speed_sample_count) {
            record->next_sample = 0;
            record->full = true;
        }
    }
    record->last_position = target->vtable->position(target);
    update_average(record);
}
