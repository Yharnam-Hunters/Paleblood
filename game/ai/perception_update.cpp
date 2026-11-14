// SPDX-License-Identifier: GPL-2.0-or-later
// AI: the perception update, once per think. It is where an enemy's target state comes from: the
// state the scripts read with IsBattleState, IsCautionState, IsFindState, IsChangeState and
// GetCurrTargetState (script_bindings.cpp).
//
// The update first lets the target-list update sense the world (sight and hearing; left
// original), then reads the target list in a fixed order of precedence:
//
//   kind-4 node           -> battle, target type 1
//   kind-5 node           -> battle, target type 2
//   main entry            -> the entry's own state (caution, find or battle), target type 3
//   kind-3 node           -> caution, target type 4
//   kind-11 node          -> caution, target type 5
//   nothing               -> normal, target type 0
//
// The state the update replaces becomes the previous state, so IsChangeState is true for exactly
// one think after a change. A state outside 0..3 leaves both states alone (the target type is
// still taken). When the state changes while the main entry is in battle with a positive value in
// its field 0x38, the character is told which target became current. Two requests the rest of
// the AI leaves in the perception's flags are served here: a check whether the main target faces
// the character (within 45 degrees of the line towards it), and "force battle", which sets the
// main entry's state to battle.
//
// Layouts known so far are in ai_think.h and ai_character.h; the fields only this update uses
// are described below, as views of those objects.
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

// The character a perception belongs to, as the perception sees it.
struct PerceptionOwner;
struct PerceptionOwnerVtable {
    void *unknown_slots_0x00[0x18 / 8];
    Vec4 (*position)(PerceptionOwner *);
    void *unknown_slots_0x20[(0x90 - 0x20) / 8];
    // Told which target became current when the state changes with the main entry in battle.
    void (*battle_target_became_current)(PerceptionOwner *, TargetHandle *);
    void *unknown_slots_0x98[(0xa8 - 0x98) / 8];
    // True: the target-list update skips its next run (see skip_list_update).
    bool (*pauses_sensing)(PerceptionOwner *);
};
static_assert(offsetof(PerceptionOwnerVtable, position) == 0x18);
static_assert(offsetof(PerceptionOwnerVtable, battle_target_became_current) == 0x90);
static_assert(offsetof(PerceptionOwnerVtable, pauses_sensing) == 0xa8);
struct PerceptionOwner {
    const PerceptionOwnerVtable *vtable;
};

// The perception record (the object ThinkPerception in ai_think.h describes), with the fields
// the update uses.
struct Perception {
    const void *vtable;
    PerceptionOwner *owner;
    TargetList targets;
    uint8_t unknown_0x20[0x12];
    uint8_t skip_list_update;     // the target-list update clears it and returns at once
    uint8_t unknown_0x33[0x7d];
    float countdown;              // seconds; counts down while positive (who reads it: unknown)
    uint8_t unknown_0xb4[0x4];
    uint16_t flags;               // requests and answers, below
    uint8_t unknown_0xba[0xa];
    TargetState current_state;
    TargetState previous_state;
    int32_t current_target_type;
    int32_t previous_target_type;
};
static_assert(offsetof(Perception, owner) == 0x8 && offsetof(Perception, targets) == 0x10);
static_assert(offsetof(Perception, skip_list_update) == 0x32 && offsetof(Perception, countdown) == 0xb0);
static_assert(offsetof(Perception, flags) == 0xb8 && offsetof(Perception, current_state) == 0xc4);
static_assert(offsetof(Perception, previous_state) == 0xc8 && offsetof(Perception, current_target_type) == 0xcc);
static_assert(offsetof(Perception, previous_target_type) == 0xd0);
static_assert(offsetof(Perception, targets) == offsetof(ThinkPerception, targets));
static_assert(offsetof(Perception, current_state) == offsetof(ThinkPerception, current_state));

constexpr uint16_t target_faces_owner = 0x400;     // answer: set by the facing check
constexpr uint16_t check_target_facing = 0x800;    // request: run the facing check each think
constexpr uint16_t force_battle = 0x8000;          // request: put the main entry in battle, once

// A target entry's state and the field the battle notice checks, which TargetNode (ai_character.h)
// does not name yet.
struct TargetEntryState {
    uint8_t unknown_0x00[0x20];
    TargetState state;
    uint8_t unknown_0x24[0x14];
    float battle_notice_value;    // the notice is sent only while this is above 0 (meaning unknown)
};
static_assert(offsetof(TargetEntryState, state) == 0x20 && offsetof(TargetEntryState, battle_notice_value) == 0x38);

TargetEntryState *entry_state(TargetNode *entry) { return reinterpret_cast<TargetEntryState *>(entry); }

// A target's facing, which TargetHandleVtable (ai_character.h) does not name yet.
struct FacingHandleVtable {
    void *unknown_slots_0x00[0x80 / 8];
    Vec4 (*position)(TargetHandle *);
    Vec4 (*forward)(TargetHandle *);
};
static_assert(offsetof(FacingHandleVtable, position) == 0x80 && offsetof(FacingHandleVtable, forward) == 0x88);

Vec4 forward_of(TargetHandle *handle)
{
    return reinterpret_cast<const FacingHandleVtable *>(handle->vtable)->forward(handle);
}

// Kinds of target-list node other than entries; what they stand for is not known yet.
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

bool has_entries(TargetList *list) { return first_node_of_kind(list, target_entry_node) != nullptr; }

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

// The handle of what a node stands for: a character's or a place's; null for other kinds.
TargetHandle *handle_of(TargetNode *node)
{
    if (node->kind == TargetKind::place) return &node->place_handle;
    if (node->kind == TargetKind::character) return &node->handle;
    return nullptr;
}

// Which node the current target state was taken from (GetCurrTargetState's target type).
enum class TargetType : int32_t {
    none = 0,
    node_kind_4 = 1,
    node_kind_5 = 2,
    main_entry = 3,
    node_kind_3 = 4,
    node_kind_11 = 5,
};

struct CurrentTarget {
    TargetState state;
    TargetType type;
};

// The state and type the precedence order in the header comment gives. With entries but none
// above priority -1 the original reads address 0x20 (it would crash); so does this.
CurrentTarget current_target(TargetList *list)
{
    if (first_node_of_kind(list, node_kind_4)) return {TargetState::battle, TargetType::node_kind_4};
    if (first_node_of_kind(list, node_kind_5)) return {TargetState::battle, TargetType::node_kind_5};
    if (has_entries(list)) return {entry_state(main_entry(list))->state, TargetType::main_entry};
    if (first_node_of_kind(list, node_kind_3)) return {TargetState::caution, TargetType::node_kind_3};
    if (first_node_of_kind(list, node_kind_11)) return {TargetState::caution, TargetType::node_kind_11};
    return {TargetState::normal, TargetType::none};
}

bool is_known_state(TargetState state) { return static_cast<uint32_t>(state) <= static_cast<uint32_t>(TargetState::battle); }

// cos(45 degrees) as the original has it: the cosine series with every term already multiplied
// out, summed group by group.
RT_GLOBAL(0x04ceb920, facing_cosine_terms_0_to_6, const Vec4);
RT_GLOBAL(0x04ceb930, facing_cosine_terms_8_to_14, const Vec4);
RT_GLOBAL(0x04ceb940, facing_cosine_terms_16_to_22, const Vec4);

float facing_cosine()
{
    return engine::add(engine::add(group_sum(facing_cosine_terms_0_to_6.get()), group_sum(facing_cosine_terms_8_to_14.get())),
                       group_sum(facing_cosine_terms_16_to_22.get()));
}

Vec4 unit(Vec4 value)
{
    const Vec4 squared = squared_length4(value);
    return normalized(value, squared, refined_length(squared));
}

// Does the main target face the owner: its forward within 45 degrees of the line from it to the
// owner (all four lanes in the dot product, as the original has it)?
bool target_faces(PerceptionOwner *owner, TargetHandle *target)
{
    const Vec4 target_position = target->vtable->position(target);
    const Vec4 to_owner = vec::subtract(owner->vtable->position(owner), target_position);
    const Vec4 forward = forward_of(target);
    return group_sum(vec::multiply(unit(to_owner), unit(forward))) > facing_cosine();
}

RT_ORIGINAL(0x016ab4c0, perception_target_list_update, void(Perception *, float frame_seconds));

}  // namespace

// The handle of the current target (in the precedence order above, kind-11 nodes excepted), or
// null: what the battle notice passes on.
extern "C" TargetHandle *bb_ai_perception_current_handle(Perception *perception)
{
    TargetList *list = &perception->targets;
    TargetNode *node = first_node_of_kind(list, node_kind_4);
    if (!node) node = first_node_of_kind(list, node_kind_5);
    if (!node) node = has_entries(list) ? main_entry(list) : first_node_of_kind(list, node_kind_3);
    return node ? handle_of(node) : nullptr;
}

extern "C" void bb_ai_perception_update(Perception *perception, float frame_seconds)
{
    PerceptionOwner *owner = perception->owner;
    TargetList *list = &perception->targets;
    if (perception->countdown > 0.0f) perception->countdown = engine::subtract(perception->countdown, frame_seconds);
    if (owner->vtable->pauses_sensing(owner)) perception->skip_list_update = 1;
    perception_target_list_update(perception, frame_seconds);

    perception->previous_target_type = perception->current_target_type;
    const CurrentTarget current = current_target(list);
    if (is_known_state(current.state)) {
        perception->previous_state = perception->current_state;
        perception->current_state = current.state;
    }
    perception->current_target_type = static_cast<int32_t>(current.type);

    if (perception->current_state != perception->previous_state && bb_ai_perception_current_handle(perception)
        && has_entries(list)) {
        // With entries but none above priority -1 this reads address 0x20, as the original does.
        TargetEntryState *main = entry_state(main_entry(list));
        if (main->state == TargetState::battle && main->battle_notice_value > 0.0f) {
            owner->vtable->battle_target_became_current(owner, bb_ai_perception_current_handle(perception));
        }
    }

    const uint16_t requests = perception->flags;
    perception->flags = requests & ~target_faces_owner;
    if (requests & check_target_facing) {
        TargetNode *main = main_entry(list);
        TargetHandle *target = main ? handle_of(main) : nullptr;
        if (target && target_faces(owner, target)) perception->flags |= target_faces_owner;
    }

    if (perception->flags & force_battle) {
        // Same null read as above when no entry has a priority above -1.
        if (has_entries(list)) entry_state(main_entry(list))->state = TargetState::battle;
        perception->flags &= ~force_battle;
    }
}

}  // namespace ai
