// SPDX-License-Identifier: GPL-2.0-or-later
// Character: the four module updates that the per-frame character update (frame_update.cpp) calls.
//
// - character_event_request_update_01e59250 (the module at chr modules +0x58): when the
//   character's animation events are ready and an event number is pending, fires the event whose
//   name is that number formatted into a wide string (built with the thread's allocator), unless
//   the character, its module flags or the throw request suppress it; then clears the number. When
//   the character's current record (picked by the definition's version) has one particular id, it
//   also clears a field of the object at chr +0x20.
// - character_counters_refill_01e5c440 (the stepped module, given the frame time): five counters
//   each gain a global per-second rate times the frame time; the fractions carry over, and each
//   counter stays between 0 and its maximum.
// - character_state_flags_update_01e4a490 (the module at chr modules +0x68): works out whether the
//   character counts as shown this frame and folds it into two sticky flags, copies a parameter
//   float, sets a third flag from the character's flags or a virtual query, counts the frame and
//   sets a link flag when its chain of objects is complete.
// - character_ai_display_update_01cd92b0 (the module at chr +0x1f8): when its readiness check
//   passes, hands the AI debug display the two positions of the character's state module, and
//   then (when the check still passes) points its two objects at its two markers.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/container.h"
#include "../engine/scalar.h"
#include "../engine/temporary_heap.h"
#include "../engine/wide_string.h"
#include "../frame_timing/frame_time.h"
#include "runtime/original.h"

namespace {

using frame_timing::FrameTime;

struct ChrIns;
struct Link0;
struct Link1;

// The module at chr modules +0x68 (and the source of the AI display's positions).
struct alignas(16) Position {
    float x, y, z, w;
};
struct ChrStateModule {
    uint8_t unknown_0x000[0x8];
    ChrIns *character;
    uint8_t unknown_0x010[0x10];
    Link0 *link_0x20;
    uint8_t unknown_0x028[0x1b8];
    Position position_0x1e0;
    Position position_0x1f0;
    uint8_t unknown_0x200[0x18];
    Link1 *link_0x218;
    uint8_t unknown_0x220[0x7c];
    uint8_t linked;
    uint8_t unknown_0x29d[0x3];
    uint32_t frame_count;
    uint8_t unknown_0x2a4[0x78];
    float param_value;
    uint8_t unknown_0x320[0x2];
    uint8_t shown_ever;
    uint8_t shown_always;
    uint8_t unknown_0x324[0x2];
    uint8_t flag_0x326;
    uint8_t unknown_0x327[0x59];
    uint8_t bits_0x380;
};
static_assert(offsetof(ChrStateModule, character) == 0x8 && offsetof(ChrStateModule, link_0x20) == 0x20);
static_assert(offsetof(ChrStateModule, position_0x1e0) == 0x1e0 && offsetof(ChrStateModule, position_0x1f0) == 0x1f0);
static_assert(offsetof(ChrStateModule, link_0x218) == 0x218 && offsetof(ChrStateModule, linked) == 0x29c);
static_assert(offsetof(ChrStateModule, frame_count) == 0x2a0 && offsetof(ChrStateModule, param_value) == 0x31c);
static_assert(offsetof(ChrStateModule, shown_ever) == 0x322 && offsetof(ChrStateModule, shown_always) == 0x323);
static_assert(offsetof(ChrStateModule, flag_0x326) == 0x326 && offsetof(ChrStateModule, bits_0x380) == 0x380);
constexpr int bit_0x380_flag_shift = 2;

// The two chains the link flag needs: link_0x218 -> +0x38 (a slot not 0x7fffffff), and
// link_0x20 -> +0x20 -> a nonzero word at +0x10.
struct SlotHolder {
    uint8_t unknown_0x00[0x30];
    int32_t slot;
};
struct Link1 {
    uint8_t unknown_0x00[0x38];
    SlotHolder *holder;
};
struct WordHolder {
    uint8_t unknown_0x00[0x10];
    uint64_t word;
};
struct Link0 {
    uint8_t unknown_0x00[0x20];
    WordHolder *holder;
};
static_assert(offsetof(SlotHolder, slot) == 0x30 && offsetof(Link1, holder) == 0x38);
static_assert(offsetof(WordHolder, word) == 0x10 && offsetof(Link0, holder) == 0x20);
constexpr int32_t no_slot = 0x7fffffff;

// The object the event request goes to: ready when +0x384 is set.
struct AnimEvents {
    uint8_t unknown_0x000[0x384];
    uint8_t ready;
};
static_assert(offsetof(AnimEvents, ready) == 0x384);

struct SuppressFlags {
    uint8_t unknown_0x00[0x10];
    uint8_t flags;
};
static_assert(offsetof(SuppressFlags, flags) == 0x10);
constexpr uint8_t module_flags_suppress = 0x20;

struct ThrowState {
    uint8_t unknown_0x00[0x20];
    uint8_t active;
};
struct ThrowRequest {
    uint8_t unknown_0x00[0x28];
    ThrowState *state;
};
static_assert(offsetof(ThrowState, active) == 0x20 && offsetof(ThrowRequest, state) == 0x28);

// The character's record: its current entry is one of two, by its definition's version.
struct RecordEntry {
    uint32_t id;
};
struct RecordEntries {
    uint8_t unknown_0x00[0x68];
    RecordEntry *entry_old;
    uint8_t unknown_0x70[0x40];
    RecordEntry *entry_new;
};
struct RecordDefinition {
    uint8_t unknown_0x00[0x88];
    const uint32_t *version;
};
struct ChrRecord {
    uint8_t unknown_0x00[0x18];
    RecordDefinition *definition;
    RecordEntries *entries;
};
static_assert(offsetof(RecordEntries, entry_old) == 0x68 && offsetof(RecordEntries, entry_new) == 0xb0);
static_assert(offsetof(RecordDefinition, version) == 0x88 && offsetof(ChrRecord, entries) == 0x20);
constexpr uint32_t new_entry_from_version = 3;
constexpr uint32_t entry_id_clearing_target = 3200101;

struct ChrModules {
    uint8_t unknown_0x00[0x8];
    SuppressFlags *suppress;
    uint8_t unknown_0x10[0x10];
    ChrRecord *record;
    uint8_t unknown_0x28[0x8];
    AnimEvents *anim_events;
    uint8_t unknown_0x38[0x30];
    ChrStateModule *state;
    uint8_t unknown_0x70[0x18];
    ThrowRequest *throw_request;
};
static_assert(offsetof(ChrModules, suppress) == 0x8 && offsetof(ChrModules, record) == 0x20);
static_assert(offsetof(ChrModules, anim_events) == 0x30 && offsetof(ChrModules, state) == 0x68);
static_assert(offsetof(ChrModules, throw_request) == 0x88);

// The character's parameters: a flag word and a float the state module copies.
struct ChrParams {
    uint8_t unknown_0x00[0x3c];
    uint32_t flags;
    uint8_t unknown_0x40[0x50];
    float value;
};
static_assert(offsetof(ChrParams, flags) == 0x3c && offsetof(ChrParams, value) == 0x90);
constexpr uint32_t params_shown = 0x10;
constexpr uint32_t params_shown_with_flags = 0x100000;

struct DrawTarget;
struct DrawTargetVtable {
    void *unknown_slots[25];
    bool (*visible)(DrawTarget *);
};
static_assert(offsetof(DrawTargetVtable, visible) == 0xc8);
struct DrawTarget {
    const DrawTargetVtable *vtable;
};

struct ChrData {
    uint8_t unknown_0x000[0x8];
    ChrIns *owner;
    uint8_t unknown_0x010[0x28];
    ChrParams *params;
    uint8_t unknown_0x040[0x40];
    DrawTarget *fallback_target;
    uint8_t unknown_0x088[0x1f8];
    DrawTarget *target;
};
static_assert(offsetof(ChrData, owner) == 0x8 && offsetof(ChrData, params) == 0x38);
static_assert(offsetof(ChrData, fallback_target) == 0x80 && offsetof(ChrData, target) == 0x280);

struct AnimState {
    uint8_t unknown_0x00[0xd8];
    uint8_t state;
};
static_assert(offsetof(AnimState, state) == 0xd8);
constexpr uint8_t anim_state_hidden = 6;

struct EventGate {
    uint8_t unknown_0x00[0x18];
    uint8_t flags;
};
struct EventGateHolder {
    uint8_t unknown_0x00[0x30];
    EventGate *gate;
};
static_assert(offsetof(EventGate, flags) == 0x18 && offsetof(EventGateHolder, gate) == 0x30);
constexpr uint8_t gate_suppress = 0x1;

struct TargetField {
    uint8_t unknown_0x00[0x4];
    int32_t value;
};
static_assert(offsetof(TargetField, value) == 0x4);
constexpr int32_t cleared_target = -1;

struct ChrInsVtable {
    void *unknown_slots[143];
    bool (*state_query)(ChrIns *);
};
static_assert(offsetof(ChrInsVtable, state_query) == 0x478);

// The character's flag byte at +0x1e1: shown outright, or decided by the parameters (instead of
// the animation state).
constexpr uint8_t chr_flag_shown = 0x40;
constexpr uint8_t chr_flag_params_decide = 0x20;

struct ChrIns {
    const ChrInsVtable *vtable;
    uint8_t unknown_0x008[0x10];
    EventGateHolder *gate_holder;
    TargetField *target_field;
    uint8_t unknown_0x028[0x30];
    ChrData *data;
    uint8_t unknown_0x060[0x181];
    uint8_t flags;
    uint8_t unknown_0x1e2[0xa6];
    AnimState *anim_state;
    uint8_t unknown_0x290[0x120];
    ChrModules *modules;
};
static_assert(offsetof(ChrIns, gate_holder) == 0x18 && offsetof(ChrIns, target_field) == 0x20);
static_assert(offsetof(ChrIns, data) == 0x58 && offsetof(ChrIns, flags) == 0x1e1);
static_assert(offsetof(ChrIns, anim_state) == 0x288 && offsetof(ChrIns, modules) == 0x3b0);

// The module at chr modules +0x58: the pending event number (none: -1).
struct EventRequestModule {
    uint8_t unknown_0x00[0x8];
    ChrIns *character;
    uint8_t unknown_0x10[0x10];
    int32_t pending_event;
};
static_assert(offsetof(EventRequestModule, character) == 0x8 && offsetof(EventRequestModule, pending_event) == 0x20);
constexpr int32_t no_event = -1;

// The stepped module: five counters with their maxima and the fractions they carry.
constexpr int counter_count = 5;
struct CounterModule {
    uint8_t unknown_0x00[0x10];
    int32_t counters[counter_count];
    int32_t maxima[counter_count];
    float fractions[counter_count];
};
static_assert(offsetof(CounterModule, counters) == 0x10 && offsetof(CounterModule, maxima) == 0x24);
static_assert(offsetof(CounterModule, fractions) == 0x38);

// The module at chr +0x1f8: an AI debug display (ai_debug_set_position's object) with two
// objects that are pointed at its two markers, and the character it follows.
struct Marker {
    int32_t entity_id;
    int32_t point_index;
};
constexpr Marker no_marker = {-1, -1};
struct MarkerObject;
struct AiDisplayModule {
    uint8_t unknown_0x00[0x8];
    MarkerObject *first_object;
    MarkerObject *second_object;
    Marker first_marker;
    Marker second_marker;
    uint8_t unknown_0x28[0x8];
    ChrIns *character;
};
static_assert(offsetof(AiDisplayModule, first_object) == 0x8 && offsetof(AiDisplayModule, second_object) == 0x10);
static_assert(offsetof(AiDisplayModule, first_marker) == 0x18 && offsetof(AiDisplayModule, second_marker) == 0x20);
static_assert(offsetof(AiDisplayModule, character) == 0x30);
constexpr int32_t no_display_mode = 0;

// The thread's heap (thread-local): its allocator is an object at +0x28.
struct ThreadHeap {
    uint8_t unknown_0x00[0x28];
    engine::Allocator allocator;
};
static_assert(offsetof(ThreadHeap, allocator) == 0x28);

RT_ORIGINAL(0x02b62d00, wide_string_format, void(engine::WideString *, const char16_t *format, ...));
RT_ORIGINAL(0x01e19f60, fire_named_event, void(AnimEvents *, const char16_t *name));
RT_GLOBAL(0x04d92c54, event_name_format, const char16_t);
RT_GLOBAL(0x0552838c, refill_per_second, const int32_t);
RT_ORIGINAL(0x01cd9390, ai_display_ready, bool(AiDisplayModule *));
RT_ORIGINAL(0x016334b0, ai_debug_set_position, void(AiDisplayModule *, Position *, Position *, int32_t mode));
RT_ORIGINAL(0x0221ff70, point_at_markers, void(MarkerObject *, const Marker *from, const Marker *to));

engine::Allocator *thread_allocator()
{
    auto &slot = engine::temporary_heap::thread_heap();
    if (!slot) engine::temporary_heap::create_for_thread();
    return &reinterpret_cast<ThreadHeap *>(engine::temporary_heap::thread_heap())->allocator;
}

bool event_suppressed(const ChrIns *chr, const ChrModules *modules)
{
    const EventGate *gate = chr->gate_holder->gate;
    if (gate && (gate->flags & gate_suppress)) return true;
    if (modules->suppress->flags & module_flags_suppress) return true;
    const ThrowState *state = modules->throw_request->state;
    return state && state->active;
}

// Formats the event number into a wide string on the thread's allocator, fires that event and
// frees the string.
void fire_numbered_event(AnimEvents *events, int32_t number)
{
    engine::Allocator *allocator = thread_allocator();
    engine::check_container_heap(allocator);
    engine::WideString name;
    name.unknown_0x00 = 0;
    name.allocator = allocator;
    name.capacity = engine::wide_string_inline_capacity - 1;
    name.size = 0;
    name.inline_text[0] = 0;
    name.owns = 1;
    wide_string_format(&name, event_name_format.address(), number);
    fire_named_event(events, name.capacity >= engine::wide_string_inline_capacity ? name.heap : name.inline_text);
    engine::wide_string_release(&name);
}

// One counter's step: the rate's share of this frame plus the carried fraction; the whole part
// is added (wrapping) and the result kept between 0 and the maximum.
void refill(int32_t &counter, int32_t maximum, float &fraction, float frame_time)
{
    const float total = engine::add(engine::multiply(static_cast<float>(refill_per_second.get()), frame_time), fraction);
    const int32_t whole = engine::truncate(total);
    fraction = engine::subtract(total, static_cast<float>(whole));
    const int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(whole) + static_cast<uint32_t>(counter));
    if (sum < 0)
        counter = 0;
    else
        counter = maximum <= sum ? maximum : sum;
}

// Whether the character counts as shown this frame.
bool chr_shown(ChrIns *chr)
{
    const uint8_t flags = chr->flags;
    if (flags & chr_flag_shown) return true;
    if (!(flags & chr_flag_params_decide) && chr->anim_state && chr->anim_state->state != anim_state_hidden)
        return true;
    ChrData *data = chr->data;
    const uint32_t params = data->params->flags;
    if (params & params_shown) return true;
    if ((flags & chr_flag_params_decide) && !(params & params_shown_with_flags)) return true;
    DrawTarget *target = data->target ? data->target : data->fallback_target;
    if (!target->vtable->visible(target)) return false;
    const ThrowState *state = chr->modules->throw_request->state;
    return !state || !state->active;
}

bool links_complete(const ChrStateModule *self)
{
    const Link1 *link1 = self->link_0x218;
    if (!link1 || !link1->holder || link1->holder->slot == no_slot) return false;
    const Link0 *link0 = self->link_0x20;
    return link0 && link0->holder && link0->holder->word;
}

}  // namespace

extern "C" void bb_character_event_request_update_01e59250(EventRequestModule *self)
{
    ChrModules *modules = self->character->modules;
    AnimEvents *events = modules->anim_events;
    if (!events || !events->ready) return;
    const int32_t number = self->pending_event;
    if (number == no_event) return;
    if (!event_suppressed(self->character, modules)) {
        fire_numbered_event(events, number);
        modules = self->character->modules;
    }
    self->pending_event = no_event;

    ChrRecord *record = modules->record;
    RecordEntries *entries = record->entries;
    if (!entries) return;
    const RecordEntry *entry =
        *record->definition->version >= new_entry_from_version ? entries->entry_new : entries->entry_old;
    if (entry->id == entry_id_clearing_target) self->character->target_field->value = cleared_target;
}

extern "C" void bb_character_counters_refill_01e5c440(CounterModule *self, const FrameTime *step)
{
    for (int i = 0; i < counter_count; i++)
        refill(self->counters[i], self->maxima[i], self->fractions[i], step->seconds);
}

extern "C" void bb_character_state_flags_update_01e4a490(ChrStateModule *self)
{
    // Byte-wise, as the original: the sticky flags may hold values other than 0 and 1.
    const uint8_t shown = static_cast<uint8_t>(chr_shown(self->character) | self->shown_ever);
    self->shown_ever = shown;
    self->shown_always &= shown;

    ChrIns *chr = self->character;
    const ChrParams *params = chr->data->params;
    self->param_value = params->value;
    if (chr->flags & chr_flag_params_decide) {
        if (params->flags & params_shown_with_flags)
            self->flag_0x326 = 1;
        else
            self->flag_0x326 = (self->bits_0x380 >> bit_0x380_flag_shift) & 1;
    } else if (chr->vtable->state_query(chr)) {
        self->flag_0x326 = 1;
    }

    self->frame_count++;
    self->linked = 0;
    if (links_complete(self)) self->linked = 1;
}

extern "C" void bb_character_ai_display_update_01cd92b0(AiDisplayModule *self)
{
    if (!ai_display_ready(self)) return;
    const ChrStateModule *state = self->character->data->owner->modules->state;
    Position first = state->position_0x1e0, second = state->position_0x1f0;
    ai_debug_set_position(self, &first, &second, no_display_mode);
    if (!ai_display_ready(self) || !self->first_object) return;
    if (MarkerObject *object = self->second_object) {
        const Marker from = no_marker;
        point_at_markers(object, &from, &self->second_marker);
        if (!self->first_object) return;
    }
    const Marker to = no_marker;
    point_at_markers(self->first_object, &self->first_marker, &to);
}
