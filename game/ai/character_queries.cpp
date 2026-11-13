// SPDX-License-Identifier: GPL-2.0-or-later
// AI: the small per-frame questions an AI character's think asks about its own character and
// about what it has noticed. Every AI-controlled character runs these once per frame (a coverage
// boot of the Iosefka's Clinic save counted the same number of calls for each: frames times AI
// characters), so they sit on the hot path of every enemy in the game.
//
// The objects are described in ai_character.h. Where the purpose of a flag is not known yet the
// functions are named for what they test, not for a guess.
#include <cstdint>

#include "../engine/scalar.h"
#include "../engine/vector_operations.h"
#include "ai_character.h"

namespace ai {
namespace {

bool is_target_entry(const TargetNode *node) { return (node->node_kind & node_kind_mask) == target_entry_node; }

// The main target: the target entry with the highest priority above -1 (the first of equals),
// or null when no entry's priority is above -1.
TargetNode *main_target(TargetList *list)
{
    TargetNode *best = nullptr;
    float best_priority = no_priority;
    for (TargetNode *node = list->first; node; node = node->next) {
        if (is_target_entry(node) && node->priority > best_priority) {
            best = node;
            best_priority = node->priority;
        }
    }
    return best;
}

float distance_squared(engine::Vec4 a, engine::Vec4 b)
{
    const engine::Vec4 d = engine::vector_operations::subtract(a, b);
    const engine::Vec4 squares = d * d;
    // x*x + y*y first, then z*z, as the original's horizontal add.
    return engine::add(engine::add(squares.x(), squares.y()), squares.z());
}

// Whether `entry` stands for the same thing as `main`: the same character (by its id), or a place
// within half a metre. Entries of different kinds, places not yet valid and characters without
// an id never match.
bool same_target(TargetNode *entry, TargetNode *main)
{
    if (entry->kind != main->kind) return false;
    if (entry->kind == TargetKind::place) {
        if (!entry->place_valid || !main->place_valid) return false;
        return distance_squared(entry->place, main->place) < same_place_distance_squared;
    }
    if (entry->kind != TargetKind::character) return false;
    const int32_t main_id = main->handle.vtable->id(&main->handle);
    if (main_id == no_target_id) return false;
    return entry->handle.vtable->id(&entry->handle) == main_id;
}

}  // namespace
}  // namespace ai

using namespace ai;

// AI view (vtable 0x057185b0, slot +0x58): the character's transform's third column, negated,
// with w = 0; the zero vector without a character.
extern "C" engine::Vec4 bb_ai_character_negated_third_column(AiCharacterView *view)
{
    const Character *character = view->character;
    if (!character) return engine::Vec4::zero();
    const Transform &m = character->transform;
    return engine::vector_operations::lanes(-m.first.z, -m.second.z, -m.third.z, 0.0f);
}

// AI view (slot +0x90): the character's id (its own slot +0x88), -1 without a character.
extern "C" int32_t bb_ai_character_id(AiCharacterView *view)
{
    Character *character = view->vtable->character(view);
    if (!character) return no_character_id;
    return character->vtable->id(character);
}

// AI view (slot +0x128): the character's flag module is enabled and one of its two flags is set.
// The view is asked for its character twice, as in the original.
extern "C" bool bb_ai_character_flag_module_set(AiCharacterView *view)
{
    if (!view->vtable->character(view)) return false;
    Character *character = view->vtable->character(view);
    // The flags live on the modules of the character that owns the character's first module.
    const CharacterFlagModule *flags = character->modules->module_08->owner->modules->flags;
    if (!flags || !flags->enabled_384) return false;
    return flags->flag_37f || flags->flag_380;
}

// AI view (vtable 0x0571b540, slot +0x30): whether an active entry of the character's effect
// list sets state `state` (0x27, 0xb0 or 0xb1); any other id is never set.
extern "C" bool bb_ai_character_has_effect_state(AiCharacterView *view, EffectState state)
{
    if (!view->character) return false;
    for (const EffectEntry *entry = view->character->effects->first; entry; entry = entry->next) {
        if ((entry->flags & inactive_entry_flags) || !entry->row) continue;
        const EffectRow *row = entry->row;
        bool set = false;
        switch (state) {
        case EffectState::state_27: set = row->state_bits_162 & state_27_bit; break;
        case EffectState::state_b0: set = row->state_bits_162 & state_b0_bit; break;
        case EffectState::state_b1: set = row->state_bits_163 & state_b1_bit; break;
        default: continue;
        }
        if (set) return true;
    }
    return false;
}

// AI view (slot +0x40): ORs the three states of every active effect entry into `states`. An active
// entry without a row still ORs zero into the third flag, as the original's store does.
extern "C" void bb_ai_character_gather_effect_states(AiCharacterView *view, EffectStates *states)
{
    if (!view->character) return;
    for (const EffectEntry *entry = view->character->effects->first; entry; entry = entry->next) {
        if (entry->flags & inactive_entry_flags) continue;
        const EffectRow *row = entry->row;
        if (row) {
            states->state_27 |= (row->state_bits_162 & state_27_bit) ? 1 : 0;
            states->state_b0 |= (row->state_bits_162 & state_b0_bit) ? 1 : 0;
        }
        states->state_b1 |= (row && (row->state_bits_163 & state_b1_bit)) ? 1 : 0;
    }
}

// AI view (slot +0x68): a test of the character behind the module at +0x78. When that character
// answers yes to query +0x1a8 and no to query +0x1c0, its mode module's mode word must be 1;
// otherwise its AI state's slot must be set.
extern "C" bool bb_ai_character_ready_check(AiCharacterView *view)
{
    if (!view->character) return false;
    CharacterModule *module = view->character->modules->module_78;
    Character *subject = module->owner;
    if (subject->vtable->query_1a8(subject)) {
        Character *again = module->owner;   // read again after each call, as the original
        if (!again->vtable->query_1c0(again)) {
            const int32_t *mode = module->owner->modules->mode->mode;
            return mode && *mode == mode_one;
        }
    }
    return module->owner->ai_block->state->slot != no_slot;
}

// The ids of the link `source` takes part in: both -1 unless it has a linked character whose
// link names a partner with an active, unblocked link module. Returns `out`.
extern "C" LinkIds *bb_ai_character_link_ids(LinkIds *out, Character *source)
{
    *out = {no_link_id, no_link_id};
    const LinkedCharacter *linked = source->vtable->linked(source);
    LinkIds ids = {no_link_id, no_link_id};
    if (linked) {
        const CharacterLink *link = linked->link;
        if (link) {
            const CharacterLinkModule *partner = link->partner->modules->link;
            if (partner->active_321 && !partner->blocked_1b8) ids = {link->first_id, link->second_id};
        }
    }
    *out = ids;
    return out;
}

// A pending AI event: when the character the event names (looked up by entity id once the lookup
// table exists) has this event's id pending, posts it to `receiver` (kind 0x1d with the event's
// value), clears the character's pending id and answers true. A failed lookup forgets the cached
// index.
extern "C" bool bb_ai_pending_event_fire(PendingEvent *event, void *receiver)
{
    Character *character = nullptr;
    if (character_lookup_table.get()) character = find_character_by_entity_id(event->entity_id, &event->cached_index);
    if (!character) {
        event->cached_index = no_cached_index;
        return false;
    }
    if (character->modules->mode->owner->ai_block->state->pending_event_id != event->event_id) return false;
    ai_post_event(receiver, ai_event_kind_pending, event->value, 0, 0);
    character->modules->mode->owner->ai_block->state->pending_event_id = no_pending_event;
    return true;
}

// Target list: marks every target entry that stands for the same character or place as the main
// target (the highest-priority entry). An entry that starts matching gets its recheck timer set to
// eight seconds if the timer had run out; entries that do not match are unmarked. The main target
// is searched again for each entry, as in the original; with no entry above priority -1 the
// original reads through a null pointer (no game state seen so far has one).
extern "C" void bb_ai_target_list_mark_main_matches(TargetList *list)
{
    for (TargetNode *node = list->first; node;) {
        TargetNode *entry = node;
        node = node->next;
        if (!is_target_entry(entry)) continue;
        if (!same_target(entry, main_target(list))) {
            entry->matches_main = 0;
            continue;
        }
        if (entry->matches_main) continue;
        entry->matches_main = 1;
        if (entry->recheck_timer <= 0.0f) entry->recheck_timer = recheck_seconds;   // NaN stays
    }
}
