// SPDX-License-Identifier: GPL-2.0-or-later
// An enemy's AI looking at its own character: the per-character AI objects that run every frame
// for every AI-controlled character (the think/perception core), the character object (the
// engine's ChrIns) as far as those objects read it, and the target list that holds what the
// character has noticed. Fields are named for what the replaced functions show; `unknown_0x...`
// is space nobody has needed yet, and a field whose meaning is not known says what is known.
//
// Two AI classes keep a pointer to their character at +0x10 and run these queries every frame:
// one with its virtual table at 0x057185b0 (slots +0x58, +0x90, +0x128 replaced here) and one
// at 0x0571b540 (slots +0x30, +0x40, +0x68).
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "runtime/original.h"

namespace ai {

struct Character;

// The block a character keeps at +0x58; its +0x38 holds per-character AI state the functions
// below read: a byte that is 0xff when unset, and an id another system posts for the AI to act
// on (-1 when none is pending).
struct CharacterAiState {
    uint8_t unknown_0x00[0x60];
    uint8_t slot;                  // 0xff: none
    uint8_t unknown_0x61[0x23];
    int32_t pending_event_id;      // -1: none
};
static_assert(offsetof(CharacterAiState, slot) == 0x60);
static_assert(offsetof(CharacterAiState, pending_event_id) == 0x84);
constexpr uint8_t no_slot = 0xff;
constexpr int32_t no_pending_event = -1;

struct CharacterAiBlock {
    uint8_t unknown_0x00[0x38];
    CharacterAiState *state;
};
static_assert(offsetof(CharacterAiBlock, state) == 0x38);

// One of the character's modules: each keeps its owner at +0x08.
struct CharacterModule {
    uint8_t unknown_0x00[0x8];
    Character *owner;
};
static_assert(offsetof(CharacterModule, owner) == 0x8);

// The module at +0x30: three flags the AI checks before it acts (meaning not yet known; named by
// offset).
struct CharacterFlagModule {
    uint8_t unknown_0x000[0x37f];
    uint8_t flag_37f;
    uint8_t flag_380;
    uint8_t unknown_0x381[0x3];
    uint8_t enabled_384;   // the other two count only while this is set
};
static_assert(offsetof(CharacterFlagModule, flag_37f) == 0x37f);
static_assert(offsetof(CharacterFlagModule, flag_380) == 0x380);
static_assert(offsetof(CharacterFlagModule, enabled_384) == 0x384);

// The module at +0x58 as seen through its owner's modules: its +0x28 points at a mode word.
struct CharacterModeModule {
    uint8_t unknown_0x00[0x8];
    Character *owner;
    uint8_t unknown_0x10[0x18];
    int32_t *mode;
};
static_assert(offsetof(CharacterModeModule, owner) == 0x8 && offsetof(CharacterModeModule, mode) == 0x28);
constexpr int32_t mode_one = 1;   // the only value the query below tests for

// The module at +0x68: two flags that decide whether the character's partner ids count.
struct CharacterLinkModule {
    uint8_t unknown_0x000[0x1b8];
    uint8_t blocked_1b8;
    uint8_t unknown_0x1b9[0x168];
    uint8_t active_321;
};
static_assert(offsetof(CharacterLinkModule, blocked_1b8) == 0x1b8);
static_assert(offsetof(CharacterLinkModule, active_321) == 0x321);

// The character's module table (ChrIns +0x3b0).
struct CharacterModules {
    uint8_t unknown_0x00[0x8];
    CharacterModule *module_08;
    uint8_t unknown_0x10[0x20];
    CharacterFlagModule *flags;
    uint8_t unknown_0x38[0x20];
    CharacterModeModule *mode;
    uint8_t unknown_0x60[0x8];
    CharacterLinkModule *link;
    uint8_t unknown_0x70[0x8];
    CharacterModule *module_78;
};
static_assert(offsetof(CharacterModules, module_08) == 0x08 && offsetof(CharacterModules, flags) == 0x30);
static_assert(offsetof(CharacterModules, mode) == 0x58 && offsetof(CharacterModules, link) == 0x68);
static_assert(offsetof(CharacterModules, module_78) == 0x78);

// An entry of the character's effect list (ChrIns +0x1c8): a linked list of active entries, each
// pointing at its parameter row. The row holds a run of state flag bits; three of them are asked
// for by id below.
struct EffectRow {
    uint8_t unknown_0x000[0x162];
    uint8_t state_bits_162;   // bit 6: state 0x27, bit 7: state 0xb0
    uint8_t state_bits_163;   // bit 0: state 0xb1
};
static_assert(offsetof(EffectRow, state_bits_162) == 0x162 && offsetof(EffectRow, state_bits_163) == 0x163);

struct EffectEntry {
    uint8_t unknown_0x00[0x1c];
    uint32_t flags;           // entries with any of `inactive_entry_flags` set are skipped
    uint8_t unknown_0x20[0x28];
    const EffectRow *row;
    uint8_t unknown_0x50[0x8];
    EffectEntry *next;
};
static_assert(offsetof(EffectEntry, flags) == 0x1c && offsetof(EffectEntry, row) == 0x48);
static_assert(offsetof(EffectEntry, next) == 0x58);
constexpr uint32_t inactive_entry_flags = 0x800c0003;

struct EffectList {
    uint8_t unknown_0x00[0x8];
    EffectEntry *first;
};
static_assert(offsetof(EffectList, first) == 0x8);

// The state flags by the id the AI asks with, and the bits that hold them.
enum class EffectState : int32_t {
    state_27 = 0x27,
    state_b0 = 0xb0,
    state_b1 = 0xb1,
};
constexpr uint8_t state_27_bit = 1 << 6;   // in state_bits_162
constexpr uint8_t state_b0_bit = 1 << 7;   // in state_bits_162
constexpr uint8_t state_b1_bit = 1 << 0;   // in state_bits_163

// The three flags gathered over every active entry.
struct EffectStates {
    uint8_t state_27;
    uint8_t state_b0;
    uint8_t state_b1;
};

struct CharacterVtable {
    void *unknown_slots_0x000[0x88 / 8];
    int32_t (*id)(Character *);   // the character's own id
    void *unknown_slots_0x090[(0x108 - 0x90) / 8];
    // Another character linked to this one (null when none).
    struct LinkedCharacter *(*linked)(Character *);
    void *unknown_slots_0x110[(0x1a8 - 0x110) / 8];
    bool (*query_1a8)(Character *);
    void *unknown_slots_0x1b0[2];
    bool (*query_1c0)(Character *);
};
static_assert(offsetof(CharacterVtable, id) == 0x88 && offsetof(CharacterVtable, linked) == 0x108);
static_assert(offsetof(CharacterVtable, query_1a8) == 0x1a8 && offsetof(CharacterVtable, query_1c0) == 0x1c0);

// One row of the character's 4x4 transform.
struct TransformRow {
    float x, y, z, w;
};
struct Transform {
    TransformRow first, second, third, fourth;
};

// The engine's character (ChrIns) as the AI reads it.
struct Character {
    const CharacterVtable *vtable;
    uint8_t unknown_0x008[0x50];
    CharacterAiBlock *ai_block;
    uint8_t unknown_0x060[0x30];
    Transform transform;      // the AI reads the third column (z of the first three rows)
    uint8_t unknown_0x0d0[0xf8];
    EffectList *effects;
    uint8_t unknown_0x1d0[0x1e0];
    CharacterModules *modules;
};
static_assert(offsetof(Character, ai_block) == 0x58 && offsetof(Character, transform) == 0x90);
static_assert(offsetof(Character, effects) == 0x1c8 && offsetof(Character, modules) == 0x3b0);

// What `linked` returns: its +0x1f8 is the link record, which names the partner character and
// two ids of the link.
struct CharacterLink {
    uint8_t unknown_0x00[0x18];
    int32_t first_id;
    int32_t second_id;
    uint8_t unknown_0x20[0x10];
    Character *partner;
};
static_assert(offsetof(CharacterLink, first_id) == 0x18 && offsetof(CharacterLink, second_id) == 0x1c);
static_assert(offsetof(CharacterLink, partner) == 0x30);

struct LinkedCharacter {
    uint8_t unknown_0x000[0x1f8];
    CharacterLink *link;
};
static_assert(offsetof(LinkedCharacter, link) == 0x1f8);

// The pair of link ids, both -1 when there is no usable link. The original returns it in memory
// (the caller passes the place), as for a class with its own copy constructor.
struct LinkIds {
    int32_t first;
    int32_t second;
};
constexpr int32_t no_link_id = -1;

// The AI-side object both classes of the header comment are: a virtual table and the character.
struct AiCharacterView;
struct AiCharacterViewVtable {
    void *unknown_slots[6];
    Character *(*character)(AiCharacterView *);
};
static_assert(offsetof(AiCharacterViewVtable, character) == 0x30);

struct AiCharacterView {
    const AiCharacterViewVtable *vtable;
    uint8_t unknown_0x08[0x8];
    Character *character;
};
static_assert(offsetof(AiCharacterView, character) == 0x10);

constexpr int32_t no_character_id = -1;

// An event the AI waits for: a character (found by its entity id, with a cached table index)
// whose AI state names this event's id. When it does, the event is posted to a receiver.
struct PendingEvent {
    uint8_t unknown_0x00[0x10];
    int32_t value;            // passed with the posted event
    uint8_t unknown_0x14[0xc];
    int32_t entity_id;        // the character to look up
    int32_t event_id;         // matched against the character's pending_event_id
    int32_t cached_index;     // the lookup's cache; -1 after a failed lookup
};
static_assert(offsetof(PendingEvent, value) == 0x10 && offsetof(PendingEvent, entity_id) == 0x20);
static_assert(offsetof(PendingEvent, event_id) == 0x24 && offsetof(PendingEvent, cached_index) == 0x28);
constexpr int32_t no_cached_index = -1;
constexpr int32_t ai_event_kind_pending = 0x1d;   // the kind the event is posted with

// The table the character lookup searches; the event check does nothing until it exists.
RT_GLOBAL(0x0593b0c8, character_lookup_table, void *);
RT_ORIGINAL(0x017c9930, find_character_by_entity_id, Character *(int32_t entity_id, int32_t *cached_index));
RT_ORIGINAL(0x0173a1f0, ai_post_event, void(void *receiver, int32_t kind, int32_t value, int32_t, int32_t));

// ---- the target list: what the character has noticed ----

// Each target entry can stand for another character (through a handle) or a place.
enum class TargetKind : int32_t {
    character = 1,
    place = 2,
};

struct TargetHandle;
struct TargetHandleVtable {
    void *unknown_slots[0xa8 / 8];
    int32_t (*id)(TargetHandle *);   // -1: no character
};
static_assert(offsetof(TargetHandleVtable, id) == 0xa8);
struct TargetHandle {
    const TargetHandleVtable *vtable;
};
constexpr int32_t no_target_id = -1;

// A node of the target list. Nodes of several kinds share the list; the low seven bits of
// `node_kind` say which, and target entries are kind 1.
struct TargetNode {
    uint8_t unknown_0x00[0x2];
    uint8_t node_kind;
    uint8_t unknown_0x03[0x5];
    TargetNode *next;
    uint8_t unknown_0x10[0x18];
    TargetKind kind;
    uint8_t unknown_0x2c[0x4];
    float priority;           // the entry with the highest priority (above -1) is the main target
    float recheck_timer;      // set to recheck_seconds when the entry starts matching the main one
    uint8_t unknown_0x38[0x9];
    uint8_t matches_main;     // this entry stands for the same thing as the main target
    uint8_t unknown_0x42[0x6];
    TargetHandle handle;
    uint8_t unknown_0x50[0x20];
    engine::Vec4 place;
    uint8_t place_valid;
};
static_assert(offsetof(TargetNode, node_kind) == 0x2 && offsetof(TargetNode, next) == 0x8);
static_assert(offsetof(TargetNode, kind) == 0x28 && offsetof(TargetNode, priority) == 0x30);
static_assert(offsetof(TargetNode, recheck_timer) == 0x34 && offsetof(TargetNode, matches_main) == 0x41);
static_assert(offsetof(TargetNode, handle) == 0x48 && offsetof(TargetNode, place) == 0x70);
static_assert(offsetof(TargetNode, place_valid) == 0x80);
constexpr uint8_t node_kind_mask = 0x7f;
constexpr uint8_t target_entry_node = 1;
constexpr float no_priority = -1.0f;
// Two places count as the same target closer than half a metre (compared squared).
constexpr float same_place_distance_squared = 0.25f;
constexpr float recheck_seconds = 8.0f;

struct TargetList {
    uint8_t unknown_0x00[0x8];
    TargetNode *first;
};
static_assert(offsetof(TargetList, first) == 0x8);

}  // namespace ai
