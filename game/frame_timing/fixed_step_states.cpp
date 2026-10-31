// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: state methods that advance their owner by one fixed frame step.
//
// Small methods drive an owner object through its "advance" method, which takes a frame-time
// descriptor. The originals hard-code 1/30 s; the 60 FPS community patches rewrite it to 1/60 s,
// which BB_TARGET_FPS=60 does here (frame_time.h). The owner is the Chalice Dungeon gate's
// state (it sits at +0x108 in SprjDungeonGateIns, whose step table names the modes its states
// select). Nine virtual methods of objects that hold a pointer to it (built next to
// SprjHolygrail), in three shapes:
//   until_idle (5 copies with the owner at +0x8, 1 at +0x10): when busy, advance; then report
//     whether it is still busy.
//   request_10: when idle, set request 10, advance, clear the request.
//   request_7 (2 copies): when idle and in states 2..5, set request 7, advance, clear the
//     request; otherwise report that it cannot.
// Four more methods find the gate through SprjHolygrail: the highest of seven event flags picks
// which of its gates is current; they advance it while busy and report on its mode.
// One method of an object that holds a pointer to the gate's slot (0x01ec3ef0) moves the state out
// of a ritual or dungeon mode with request 7, and out of the error mode with request 10, around a
// check on the owner (0x01ea5280, which reads FrpgNetMan).
// And five methods of SprjDungeonGateIns itself: two steps of its step machine
// (STEP_Init_forChannelLoad, STEP_ModeSwitch) and three handlers of events it registered for,
// which store whether their request was taken. None of the five returns anything: on every path
// the originals leave only the stack guard's value in rax (or end in a void virtual call).
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "frame_time.h"
#include "runtime/original.h"

namespace {

using frame_timing::FrameTime;

struct StateOwner;
struct StateOwnerVtable {
    void *unknown_slots[26];
    void (*advance)(StateOwner *, FrameTime *);
};
static_assert(offsetof(StateOwnerVtable, advance) == 0xd0);

// The gate's modes, as STEP_ModeSwitch reads them from the owner's state.
enum GateMode : uint32_t {
    mode_channel_load = 0,
    mode_no_holygrail = 1,
    mode_local_ritual = 2,
    mode_server_ritual = 3,
    mode_local_dungeon = 4,
    mode_server_dungeon = 5,
    mode_display_error = 6,
};

constexpr uint32_t slot_count = 16;  // the owner's slots, which the ritual slot reset lists

struct StateOwner {
    const StateOwnerVtable *vtable;
    uint8_t unknown_0x008[0xc8];
    engine::RefObject *handle;  // what the owner check looked up for its id (0x01ea5280)
    uint8_t unknown_0x0d8[0xb0];
    struct Slot {
        uint32_t key;
        uint32_t active;  // a slot counts when nonzero
        uint8_t unknown_0x008[0x4];
    } slots[slot_count];  // what the ritual slot reset (0x01ea5850) collects
    uint8_t unknown_0x248[0x14];
    uint32_t state;  // a GateMode
    uint32_t request;
    int32_t busy;
    uint8_t unknown_0x268[0x4];
    float value_26c;  // set to 10 with request 2; its meaning is not known
};
static_assert(offsetof(StateOwner, handle) == 0xd0 && offsetof(StateOwner, slots) == 0x188 &&
              sizeof(StateOwner::Slot) == 12);
static_assert(offsetof(StateOwner, state) == 0x25c && offsetof(StateOwner, request) == 0x260 &&
              offsetof(StateOwner, busy) == 0x264 && offsetof(StateOwner, value_26c) == 0x26c);

// The methods' own objects: the owner at +0x8, or at +0x10 for one of them.
struct StateAt8 {
    uint8_t unknown_0x00[0x8];
    StateOwner *owner;
};
struct StateAt10 {
    uint8_t unknown_0x00[0x10];
    StateOwner *owner;
};
static_assert(offsetof(StateAt8, owner) == 0x8 && offsetof(StateAt10, owner) == 0x10);

// The events the gate's handlers answer: the event's id, and flags it must carry.
struct GateEvent {
    uint8_t unknown_0x00[0x18];
    uint64_t flags;
};
static_assert(offsetof(GateEvent, flags) == 0x18);
constexpr uint64_t gate_event_required_flags = 0x1400;
constexpr int32_t gate_event_0 = 0, gate_event_1000 = 1000, gate_event_3000 = 3000;

struct DungeonGate;
struct DungeonGateVtable {
    void *unknown_slots[5];
    void (*set_step)(DungeonGate *, int32_t step);  // an index into the step table
    void *unknown_slot_30;
    void (*method_38)(DungeonGate *);  // called after STEP_Init_forChannelLoad and a mode switch
};
static_assert(offsetof(DungeonGateVtable, set_step) == 0x28 && offsetof(DungeonGateVtable, method_38) == 0x38);

// SprjDungeonGateIns: a step machine (its base counts steps at +0x50) with the state at +0x108.
struct DungeonGate {
    const DungeonGateVtable *vtable;
    uint8_t unknown_0x008[0x100];
    StateOwner owner;
    bool request_taken;  // what the last event handler's request did
    uint8_t unknown_0x379[0x327];
    int32_t value_6a0;  // reset by the handler of event 3000
};
static_assert(offsetof(DungeonGate, owner) == 0x108 && offsetof(DungeonGate, request_taken) == 0x378 &&
              offsetof(DungeonGate, value_6a0) == 0x6a0);

// 0x01ec3ef0's object: at +0x8, the slot that points to the gate.
struct GateSlotAt8 {
    uint8_t unknown_0x00[0x8];
    DungeonGate **gate;
};
static_assert(offsetof(GateSlotAt8, gate) == 0x8);

// SprjHolygrail (the Chalice Dungeons) keeps its gates in an array; seven event flags from 9020
// say which one is current (the highest flag set; none set selects the last).
struct Holygrail {
    uint8_t unknown_0x00[0x8];
    DungeonGate **gates;
};
static_assert(offsetof(Holygrail, gates) == 0x8);
struct EventFlagMan;

RT_GLOBAL(0x0593e9e8, holygrail_instance, Holygrail *);
RT_GLOBAL(0x04d3448c, holygrail_name, const char);
RT_GLOBAL(0x0593b100, event_flag_man_instance, EventFlagMan *);
RT_GLOBAL(0x04d3b19a, event_flag_man_name, const char);
// The bits of `count` event flags starting at `first`.
RT_ORIGINAL(0x017cfd80, event_flag_read_bits, uint32_t(EventFlagMan *, uint32_t first, uint32_t count));
// A question about the gate's state (it reads FrpgNetMan in some modes); one caller asks it and
// ignores the answer.
RT_ORIGINAL(0x01ea5570, gate_state_query, bool(StateOwner *));

constexpr uint32_t gate_flag_first = 9020, gate_flag_count = 7;

// A check on the owner with an id (0x01ea5280, replaced below; 0x01ec3ef0 calls it through its
// original address).
struct SessionId;
RT_ORIGINAL(0x01ea5280, gate_owner_check, bool(StateOwner *, SessionId *id));

// FrpgNetMan, the online manager: four of its bytes gate the owner check, each tested for zero
// (their meaning is not known).
struct FrpgNetMan {
    uint8_t unknown_0x000[0x8];
    uint8_t flag_008;  // must be nonzero
    uint8_t unknown_0x009[0x9ed];
    uint8_t flag_9f6;  // must be zero
    uint8_t unknown_0x9f7[0x1];
    uint8_t flag_9f8;  // must be zero
    uint8_t unknown_0x9f9[0x57];
    uint8_t flag_a50;  // must be zero
};
static_assert(offsetof(FrpgNetMan, flag_008) == 0x8 && offsetof(FrpgNetMan, flag_9f6) == 0x9f6 &&
              offsetof(FrpgNetMan, flag_9f8) == 0x9f8 && offsetof(FrpgNetMan, flag_a50) == 0xa50);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// The list the entry check (0x01ea4a10) searches: entries of 0x150 bytes, each a key and an object.
struct GateEntry {
    int32_t key;
    uint8_t unknown_0x004[0x4];
    engine::RefObject *object;
    uint8_t unknown_0x010[0x140];
};
static_assert(sizeof(GateEntry) == 0x150 && offsetof(GateEntry, object) == 0x8);
struct GateEntryList {
    uint8_t unknown_0x000[0x10];
    GateEntry *begin;
    GateEntry *end;
};
static_assert(offsetof(GateEntryList, begin) == 0x10 && offsetof(GateEntryList, end) == 0x18);

// What the owner's handle points to, as the gate methods read it (0x01ea5670, 0x01ea5850,
// 0x01ea5c90, 0x01ea5e20, 0x01ea6080): a reference-counted object with a flag bit and words whose
// meanings are not known.
struct HandleObject {
    engine::RefObject base;
    uint8_t unknown_0x010[0x40];
    uint32_t word_050;  // must be zero for the readiness check (server modes) and to pass the handle on
    uint8_t unknown_0x054[0xb8];
    uint8_t flags_10c;  // bit 0 stops the reset in the server ritual mode
    uint8_t unknown_0x10d[0xf];
    uint32_t value_11c;  // cleared before the handle is passed on (0x01ea5e20, 0x01ea6080)
    uint32_t value_120;  // likewise
    uint32_t value_124;  // cleared by the ritual resets
};
static_assert(offsetof(HandleObject, word_050) == 0x50 && offsetof(HandleObject, flags_10c) == 0x10c &&
              offsetof(HandleObject, value_11c) == 0x11c && offsetof(HandleObject, value_120) == 0x120 &&
              offsetof(HandleObject, value_124) == 0x124);
HandleObject *as_handle_object(engine::RefObject *handle) { return reinterpret_cast<HandleObject *>(handle); }

// Called with the handle's address and a kind (1 or 2) when the handle is passed on, in the local
// modes (0x02265170) or the server modes (0x02265960); not replaced, and what they do is not
// analyzed (their results are ignored).
RT_ORIGINAL(0x02265170, pass_on_in_local_mode, void(engine::RefObject **handle, int32_t kind));
RT_ORIGINAL(0x02265960, pass_on_in_server_mode, void(engine::RefObject **handle, int32_t kind));
// One of the owner's active slots as the ritual slot reset collects it: the slot's key, its index,
// and the caller's value for that index.
struct SlotItem {
    uint32_t key;
    uint32_t index;
    uint32_t value;
};
static_assert(sizeof(SlotItem) == 12);

// An engine heap, as a container keeps it: slot 0x20 reports its traits (bit 5 of the first byte:
// containers may use it), slot 0x70 frees a block.
struct Heap;
struct HeapTraits {
    uint8_t bits;
    uint8_t unknown_0x001[0xf];  // the original leaves 16 bytes for the report (its stack canary follows)
};
constexpr uint8_t heap_allows_containers = 0x20;
struct HeapVtable {
    uint8_t unknown_0x000[0x20];
    void (*report_traits)(HeapTraits *out, Heap *heap, int32_t zero);
    uint8_t unknown_0x028[0x48];
    void (*free)(Heap *heap, void *block);
};
static_assert(offsetof(HeapVtable, report_traits) == 0x20 && offsetof(HeapVtable, free) == 0x70);
struct Heap {
    const HeapVtable *vtable;
};

// The heap the slot reset's list is built on: made on first use (0x02fa2b10).
RT_GLOBAL(0x05940670, container_heap, Heap *);
RT_ORIGINAL(0x02fa2b10, make_container_heap, Heap *());
RT_GLOBAL(0x04b37dc0, message_incompatible_heap, const char);  // "Tried to create container with incompatible heap."
constexpr int32_t line_incompatible_heap = 0x3e;

// The engine's growable list of slot items. The original leaves its first 8 bytes as they were on
// the stack; the replacement zeroes them, which nothing observes (0x01e9d960 does not read them).
struct SlotItemList {
    uint64_t unknown_0x000;
    SlotItem *begin;
    SlotItem *end;
    SlotItem *capacity;
    Heap *heap;
};
static_assert(offsetof(SlotItemList, begin) == 0x8 && offsetof(SlotItemList, heap) == 0x20);
// Appends a copy of an item (0x01e9d960, not replaced).
RT_ORIGINAL(0x01e9d960, append_slot_item, void(SlotItemList *list, const SlotItem *item));
// Called with the handle's address and the list (0x01e9cd20, not replaced; not analyzed).
RT_ORIGINAL(0x01e9cd20, pass_handle_on_with_items, void(engine::RefObject **handle, SlotItemList *list));

// Called with the handle's address by the reset, which ignores its result; not replaced, and what
// it does is not analyzed (the name only says how the reset uses it).
RT_ORIGINAL(0x01e9cb80, pass_handle_on, void(engine::RefObject **handle));

// What the handle creation (0x01ea47a0) is given: an object with a word at +0x10 that it passes on
// (its meaning, like the object's, is not known).
struct CreationSource {
    uint8_t unknown_0x000[0x10];
    uint32_t value_010;
};
// The two blocks the creation fills on its stack before creating the handle object. The original
// leaves the first 0x20 bytes before the second block (nothing else there is live yet) and the
// second 72 bytes, up to its stack canary.
constexpr size_t creation_part1_bytes = 0x20, creation_part2_bytes = 0x48;
struct CreationPart1 {
    uint8_t bytes[creation_part1_bytes];
};
struct CreationPart2 {
    uint8_t bytes[creation_part2_bytes];
};
RT_ORIGINAL(0x0231fbc0, fill_creation_part1, void(CreationPart1 *out, CreationSource *source, uint32_t value));
RT_ORIGINAL(0x01eaf9d0, fill_creation_part2, void(CreationPart2 *out, CreationSource *source));
// Creates the handle object, holding a reference the caller drops (or null); not replaced.
RT_ORIGINAL(0x01e9a8f0, create_handle_object,
            void(engine::RefObject **out, CreationPart1 *part1, CreationSource *source, CreationPart2 *part2,
                 void *net_block, const void *name));

// The engine's string as the creation reads a name from it: the text is inline while the capacity
// is below 8, else at the pointer stored there (whether it is narrow or wide is not known).
constexpr size_t engine_string_inline_bytes = 0x10;
struct EngineString {
    uint8_t unknown_0x000[0x8];
    union {
        uint8_t inline_text[engine_string_inline_bytes];
        const void *heap_text;
    };
    uint8_t unknown_0x018[0x8];
    uint64_t capacity;
};
static_assert(offsetof(EngineString, inline_text) == 0x8 && offsetof(EngineString, capacity) == 0x20);
constexpr uint64_t engine_string_inline_capacity = 8;
const void *text_of(const EngineString *string)
{
    return string->capacity >= engine_string_inline_capacity ? string->heap_text : string->inline_text;
}

// The entry the creation takes its name from: kind 1 names it at +0xc8, any other kind at +0x18.
struct NameEntry {
    uint8_t unknown_0x000[0xc];
    int32_t kind;
    uint8_t unknown_0x010[0x8];
    EngineString name_018;
    uint8_t unknown_0x040[0x88];
    EngineString name_0c8;
};
static_assert(offsetof(NameEntry, name_018) == 0x18 && offsetof(NameEntry, name_0c8) == 0xc8);
// The singleton next to FrpgNetMan (0x0593b130, its class not known; read without a check): its
// object at +8 points at +0x5d0 to where the current name entry is kept (null for none).
struct NameHolder {
    uint8_t unknown_0x000[0x5d0];
    NameEntry **current;
};
struct NameRoot {
    uint8_t unknown_0x000[0x8];
    NameHolder *holder;
};
static_assert(offsetof(NameHolder, current) == 0x5d0 && offsetof(NameRoot, holder) == 0x8);
RT_GLOBAL(0x0593b130, name_root_instance, NameRoot *);
RT_GLOBAL(0x04dd85f0, default_name, const uint8_t);  // used when there is no entry
// FrpgNetMan's block at +0xa68, which the creation passes on.
constexpr size_t frpg_net_man_creation_block = 0xa68;

// The id the owner check is given: its first 64 bits hold INT64_MIN when there is none. That it
// names an online session is a guess from FrpgNetMan's part in the check.
struct SessionId {
    int64_t value;
};
constexpr int64_t no_session_id = INT64_MIN;
// Fills *found with the object for an id, holding a reference the caller drops (or with null).
RT_ORIGINAL(0x02265290, lookup_session_object, void(engine::RefObject **found, SessionId *id));

// The step machine's step counter; past 27 steps it also builds a debug message.
RT_ORIGINAL(0x01ec3000, step_machine_count_step, void(DungeonGate *));

// What the methods return (the state machine's codes; their names are not known).
constexpr uint32_t result_still_busy = 1, result_done = 2, result_not_now = 4;
// The requests the owner is given while it advances.
constexpr uint32_t request_none = 0, request_1 = 1, request_2 = 2, request_3 = 3, request_4 = 4, request_6 = 6, request_7 = 7, request_8 = 8, request_9 = 9,
                   request_10 = 10;
// What the handler of event 3000 leaves in value_6a0.
constexpr int32_t value_6a0_reset = -1;
// What request 2 sets value_26c to.
constexpr float request_2_value = 10.0f;

// Indices into SprjDungeonGateIns's step table (the steps that start each mode).
enum GateStep : int32_t {
    step_init_for_channel_load = 1,
    step_init_for_disp_error = 5,
    step_init_for_no_holygrail = 8,
    step_init_for_local_ritual = 11,
    step_init_for_server_ritual = 14,
    step_init_for_local_dungeon = 17,
    step_init_for_server_dungeon = 20,
};

// The step STEP_ModeSwitch goes to for each mode, in GateMode order.
constexpr GateStep mode_first_step[] = {
    step_init_for_channel_load,  step_init_for_no_holygrail,   step_init_for_local_ritual,
    step_init_for_server_ritual, step_init_for_local_dungeon,  step_init_for_server_dungeon,
    step_init_for_disp_error,
};
static_assert(sizeof mode_first_step / sizeof mode_first_step[0] == mode_display_error + 1);
// request_7 runs only in these states.
constexpr uint32_t request_7_first_state = 2, request_7_last_state = 5;

void advance(StateOwner *owner)
{
    FrameTime step = frame_timing::fixed_frame_time();
    owner->vtable->advance(owner, &step);
}

uint32_t until_idle(StateOwner *owner)
{
    if (owner->busy == 0) return result_done;
    advance(owner);
    return owner->busy != 0 ? result_still_busy : result_done;
}

void advance_with_request_10(StateOwner *owner)
{
    if (owner->busy != 0) return;
    owner->request = request_10;
    advance(owner);
    owner->request = request_none;
}

// The owner's requests that report whether they were taken (true) or not now (false).
bool take_request(StateOwner *owner, uint32_t request)
{
    owner->request = request;
    advance(owner);
    owner->request = request_none;
    return true;
}

bool take_request_1(StateOwner *owner)
{
    if (owner->state == mode_channel_load || owner->state == mode_display_error || owner->busy != 0) return false;
    return take_request(owner, request_1);
}

bool take_request_2(StateOwner *owner)
{
    if (owner->busy != 0 || owner->state != mode_channel_load) return false;
    owner->value_26c = request_2_value;
    return take_request(owner, request_2);
}

bool take_request_7(StateOwner *owner)
{
    if (owner->busy != 0 || owner->state < request_7_first_state || owner->state > request_7_last_state) return false;
    return take_request(owner, request_7);
}

bool take_request_10(StateOwner *owner)
{
    if (owner->busy != 0) return false;
    return take_request(owner, request_10);
}

// The current gate: index gate_flag_count minus the bit length of the flags (the index stays
// unsigned, as in the original, so more than seven bits would index far past the array).
DungeonGate *current_gate()
{
    Holygrail *holygrail = engine::require(holygrail_instance, holygrail_name.address());
    EventFlagMan *flags = engine::require(event_flag_man_instance, event_flag_man_name.address());
    uint32_t bits = event_flag_read_bits(flags, gate_flag_first, gate_flag_count);
    uint32_t bit_length = 0;
    for (; bits != 0; bits >>= 1) bit_length++;
    const uint32_t index = gate_flag_count - bit_length;
    return holygrail->gates[index];
}

// The ritual and dungeon modes come in pairs: local (even) and server (the next, odd). Both match.
constexpr uint32_t server_variant_bit = 1;
bool in_ritual_mode(uint32_t state) { return (state & ~server_variant_bit) == mode_local_ritual; }
bool in_dungeon_mode(uint32_t state) { return (state & ~server_variant_bit) == mode_local_dungeon; }

bool is_gate_event(const GateEvent *event, int32_t id, int32_t wanted)
{
    return id == wanted && (event->flags & gate_event_required_flags) == gate_event_required_flags;
}

uint32_t advance_with_request_7(StateOwner *owner)
{
    if (owner->busy != 0 || owner->state < request_7_first_state || owner->state > request_7_last_state)
        return result_not_now;
    owner->request = request_7;
    advance(owner);
    owner->request = request_none;
    return result_done;
}

void advance_if_busy(StateOwner *owner)
{
    if (owner->busy != 0) advance(owner);
}

// Out of the error mode: request 10 when idle, then one more advance while busy.
void leave_error_mode(StateOwner *owner)
{
    if (owner->state != mode_display_error) return;
    advance_with_request_10(owner);
    advance_if_busy(owner);
}

// Out of a ritual or dungeon mode with request 7; if that was taken, settle, and leave the error
// mode the request may have led to.
void leave_ritual_or_dungeon_mode(StateOwner *owner)
{
    if (advance_with_request_7(owner) != result_done) return;
    advance_if_busy(owner);
    leave_error_mode(owner);
}

// FrpgNetMan's four bytes, as the online methods test them ("online" is a guess from FrpgNetMan's
// part). A missing FrpgNetMan is reported and the singleton read again, as the original does
// (engine::require).
bool net_allows()
{
    const FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    return net->flag_008 != 0 && net->flag_9f8 == 0 && net->flag_9f6 == 0 && net->flag_a50 == 0;
}

// The gates both online checks open with: the owner idle in mode 1 (no Holygrail), then
// FrpgNetMan's four bytes.
bool online_gates_open(const StateOwner *owner)
{
    if (owner->busy != 0 || owner->state != mode_no_holygrail) return false;
    return net_allows();
}

// Keeps an entry's object as the owner's handle: an unchanged handle is left alone; otherwise the
// old one is released and the entry's object is read again (releasing may have run a destructor),
// stored, and retained.
void keep_entry_object(StateOwner *owner, const GateEntry *entry)
{
    engine::RefObject *object = entry->object;
    if (owner->handle == object) return;
    if (owner->handle) {
        engine::release(owner->handle);
        owner->handle = nullptr;
        object = entry->object;
    }
    owner->handle = object;
    if (object) engine::retain(object);
}

// Makes an object the caller holds a reference to the owner's handle, in the original's order of
// reference counts: an unchanged handle drops the caller's reference; otherwise the old handle is
// released and cleared, the object stored, and a reference taken and the caller's dropped (the
// pair leaves the count as it was but destroys an object that arrives with no references).
void adopt_handle(StateOwner *owner, engine::RefObject *held)
{
    if (owner->handle == held) {
        if (held) engine::release(held);
        return;
    }
    if (owner->handle) {
        engine::release(owner->handle);
        owner->handle = nullptr;
    }
    owner->handle = held;
    if (held) {
        engine::retain(held);
        engine::release(held);
    }
}

// Keeps the object found for an id as the owner's handle, in the original's order of reference
// counts: the lookup's reference is swapped for one of the owner's own; an unchanged handle drops
// the extra one. Each retain-then-release pair leaves the count as it was but is the original's
// exact sequence: an object that arrives with no references is destroyed by it (or reported).
void keep_handle(StateOwner *owner, engine::RefObject *found)
{
    engine::RefObject *held = nullptr;
    if (found) {
        engine::retain(found);
        engine::release(found);  // the lookup's reference
        held = found;
    }
    adopt_handle(owner, held);
}

}  // namespace

extern "C" uint32_t bb_frame_timing_until_idle_01f6a9f0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_0200e100(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_02012610(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_020128f0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_02012bf0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_0200d8a0(StateAt10 *self) { return until_idle(self->owner); }

extern "C" void bb_frame_timing_request_10_02012780(StateAt8 *self) { advance_with_request_10(self->owner); }
extern "C" uint32_t bb_frame_timing_request_7_0200e270(StateAt8 *self) { return advance_with_request_7(self->owner); }
extern "C" uint32_t bb_frame_timing_request_7_02012a60(StateAt8 *self) { return advance_with_request_7(self->owner); }

// The owner check (0x01ea5280): when the online gates are open and the id is set, keeps the object
// found for the id as the owner's handle, then requests 6 if still idle and reports true; every
// other path reports false.
extern "C" bool bb_frame_timing_gate_owner_check(StateOwner *owner, SessionId *id)
{
    if (!online_gates_open(owner)) return false;
    if (id->value == no_session_id) return false;
    engine::RefObject *found = nullptr;
    lookup_session_object(&found, id);
    keep_handle(owner, found);
    if (owner->busy != 0) return false;
    return take_request(owner, request_6);
}

// The entry check (0x01ea4a10): when the online gates are open, finds the first entry with the key
// that holds an object, keeps that object as the owner's handle, then requests 4 if still idle and
// reports true; no such entry, or any other path, reports false. The list's end is read once, as
// the original does. Before keeping a match it checks FrpgNetMan again: a missing one is reported,
// then the entry's object is read again and the entry kept if it still holds one, otherwise the
// search goes on (0x01ea4b17; decompilers that treat the report as not returning hide this).
extern "C" bool bb_frame_timing_gate_entry_check(StateOwner *owner, GateEntryList *list, int32_t key)
{
    if (!online_gates_open(owner)) return false;
    GateEntry *const end = list->end;
    for (GateEntry *entry = list->begin; entry != end; ++entry) {
        if (entry->key != key || !entry->object) continue;
        if (!frpg_net_man_instance.get()) {
            engine::report_missing(frpg_net_man_name.address());
            if (!entry->object) continue;
        }
        keep_entry_object(owner, entry);
        if (owner->busy != 0) return false;
        return take_request(owner, request_4);
    }
    return false;
}

namespace {

// When either ritual reset may run: the owner idle in the local ritual mode, or in the server
// ritual mode with FrpgNetMan's bytes allowing it and the handle's flag bit 0 clear. The original
// checks FrpgNetMan again with the pointer the bytes were just read through, so that check cannot
// fail and is left out.
bool ritual_reset_allowed(const StateOwner *owner)
{
    if (owner->busy != 0) return false;
    if (owner->state == mode_local_ritual) return true;
    if (owner->state != mode_server_ritual || !net_allows()) return false;
    const auto *object = reinterpret_cast<const HandleObject *>(owner->handle);
    return !object || (object->flags_10c & 1) == 0;
}

// Before each use of the handle the original checks FrpgNetMan: a missing one is reported, then
// the handle read again from the owner (code after the fatal error call that Ghidra drops;
// QUIRKS.md). Returns the handle to go on with, which may now be null.
engine::RefObject *handle_after_net_check(const StateOwner *owner, engine::RefObject *handle)
{
    if (frpg_net_man_instance.get()) return handle;
    engine::report_missing(frpg_net_man_name.address());
    return owner->handle;
}

// Clears the handle's word at +0x124, if there is a handle (and still is after the check).
void clear_handle_word(StateOwner *owner)
{
    if (!owner->handle) return;
    if (engine::RefObject *handle = handle_after_net_check(owner, owner->handle))
        as_handle_object(handle)->value_124 = 0;
}

// The readiness check (0x01ea5c90, replaced below as its own method too): the owner idle in a
// local ritual or dungeon mode with FrpgNetMan's bytes allowing it, or in a server one with them
// allowing it and a handle whose word at +0x50 is zero. The original checks FrpgNetMan again with
// the pointer the bytes were just read through before reading the handle; that cannot fail and is
// left out.
bool ritual_or_dungeon_ready(const StateOwner *owner)
{
    if (owner->busy != 0) return false;
    switch (owner->state) {
    case mode_local_ritual:
    case mode_local_dungeon:
        return net_allows();
    case mode_server_ritual:
    case mode_server_dungeon: {
        if (!net_allows()) return false;
        const auto *object = reinterpret_cast<const HandleObject *>(owner->handle);
        return object && object->word_050 == 0;
    }
    default:
        return false;
    }
}

// Passing the handle on (0x01ea5e20 with kind 1, 0x01ea6080 with kind 2): when ready, clears the
// handle's words at +0x11c and +0x120, passes it on for the mode (local or server) if its word at
// +0x50 is zero, then requests 9 if still idle and reports true. Each use of the handle follows
// the FrpgNetMan check above; a handle gone after one skips the steps that follow.
bool pass_handle_on_with_kind(StateOwner *owner, int32_t kind)
{
    if (!ritual_or_dungeon_ready(owner)) return false;
    engine::RefObject *held = nullptr;
    if (engine::RefObject *handle = owner->handle) {
        handle = handle_after_net_check(owner, handle);
        if (handle) {
            as_handle_object(handle)->value_11c = 0;
            handle = handle_after_net_check(owner, handle);
            if (handle) {
                as_handle_object(handle)->value_120 = 0;
                held = handle;
            }
        }
    }
    // The original switches on the state again; after the readiness check its default cannot be reached.
    bool local_mode = false;
    switch (owner->state) {
    case mode_local_ritual:
    case mode_local_dungeon:
        local_mode = true;
        break;
    case mode_server_ritual:
    case mode_server_dungeon:
        break;
    default:
        return false;
    }
    if (held) held = handle_after_net_check(owner, held);
    if (held && as_handle_object(held)->word_050 == 0) {
        if (local_mode)
            pass_on_in_local_mode(&owner->handle, kind);
        else
            pass_on_in_server_mode(&owner->handle, kind);
    }
    if (owner->busy != 0) return false;
    return take_request(owner, request_9);
}

}  // namespace

// The handle creation (0x01ea47a0, also called from 0x01eb9270 and 0x01f038b0): when the owner is
// idle in mode 1 (no Holygrail), drops its handle, fills two blocks from the source, creates a
// handle object from them, FrpgNetMan's block at +0xa68 and the current entry's name (or the
// default), adopts it as the owner's handle, then requests 3 if still idle and reports true.
extern "C" bool bb_frame_timing_gate_create_handle(StateOwner *owner, CreationSource *source)
{
    if (owner->busy != 0 || owner->state != mode_no_holygrail) return false;
    if (owner->handle) {
        engine::release(owner->handle);
        owner->handle = nullptr;
    }
    NameHolder *const holder = name_root_instance.get()->holder;
    CreationPart1 part1;
    fill_creation_part1(&part1, source, source->value_010);
    CreationPart2 part2;
    fill_creation_part2(&part2, source);
    FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    void *const net_block = reinterpret_cast<uint8_t *>(net) + frpg_net_man_creation_block;
    const NameEntry *entry = *holder->current;
    const void *name = default_name.address();
    if (entry) name = text_of(entry->kind == 1 ? &entry->name_0c8 : &entry->name_018);
    engine::RefObject *created = nullptr;
    create_handle_object(&created, &part1, source, &part2, net_block, name);
    adopt_handle(owner, created);
    if (owner->busy != 0) return false;
    return take_request(owner, request_3);
}

// The readiness check as its own method (0x01ea5c90, also called from 0x01ebecd0).
extern "C" bool bb_frame_timing_gate_ritual_or_dungeon_ready(StateOwner *owner) { return ritual_or_dungeon_ready(owner); }

// Passing the handle on with kind 1 (0x01ea5e20) and kind 2 (0x01ea6080): see pass_handle_on_with_kind.
constexpr int32_t pass_on_kind_1 = 1, pass_on_kind_2 = 2;
extern "C" bool bb_frame_timing_gate_pass_on_kind_1(StateOwner *owner) { return pass_handle_on_with_kind(owner, pass_on_kind_1); }
extern "C" bool bb_frame_timing_gate_pass_on_kind_2(StateOwner *owner) { return pass_handle_on_with_kind(owner, pass_on_kind_2); }

// The ritual handle reset (0x01ea5670): when the owner is idle in the local ritual mode, or in the
// server ritual mode with FrpgNetMan's bytes allowing it and the handle's flag bit 0 clear, clears the
// handle's word at +0x124 (if there is a handle), passes the handle on to 0x01e9cb80, then requests 8
// if still idle and reports true; every other path reports false.
extern "C" bool bb_frame_timing_gate_ritual_handle_reset(StateOwner *owner)
{
    if (!ritual_reset_allowed(owner)) return false;
    clear_handle_word(owner);
    pass_handle_on(&owner->handle);
    if (owner->busy != 0) return false;
    return take_request(owner, request_8);
}

// The ritual slot reset (0x01ea5850): allowed as the ritual handle reset is, it lists the owner's
// active slots (each with the caller's value for its index) on the container heap; with none it
// reports false. Otherwise it clears the handle's word, passes the handle and the list to
// 0x01e9cd20, requests 8 if still idle and reports true. The list's block is freed last: the one
// it held before that call, as the original keeps it.
extern "C" bool bb_frame_timing_gate_ritual_slot_reset(StateOwner *owner, const uint32_t *values)
{
    if (!ritual_reset_allowed(owner)) return false;
    Heap *&heap = container_heap.get();
    if (!heap) heap = make_container_heap();
    Heap *const list_heap = heap;
    HeapTraits traits;
    list_heap->vtable->report_traits(&traits, list_heap, 0);
    if ((traits.bits & heap_allows_containers) == 0)
        engine::fatal_error(nullptr, line_incompatible_heap, message_incompatible_heap.address());

    SlotItemList list{};
    list.heap = list_heap;
    for (uint32_t index = 0; index < slot_count; ++index) {
        const StateOwner::Slot &slot = owner->slots[index];
        if (slot.active == 0) continue;
        const SlotItem item{slot.key, index, values[index]};
        append_slot_item(&list, &item);
    }
    SlotItem *const block = list.begin;
    bool requested = false;
    if (list.begin != list.end) {
        clear_handle_word(owner);
        pass_handle_on_with_items(&owner->handle, &list);
        if (owner->busy == 0) requested = take_request(owner, request_8);
    }
    if (block) list.heap->vtable->free(list.heap, block);
    return requested;
}

// Leaves a ritual or dungeon mode, then asks the check on the owner: when it says yes, settles and
// reports true in the server dungeon mode, otherwise leaves the error mode. Every other path leaves
// the ritual or dungeon mode again and reports false. The original sets only the low byte of eax
// for true (al = 1); its only caller (0x018bb383) ignores the result.
extern "C" bool bb_frame_timing_leave_modes_around_owner_check_01ec3ef0(GateSlotAt8 *self, SessionId *id)
{
    StateOwner *owner = &(*self->gate)->owner;
    leave_ritual_or_dungeon_mode(owner);
    if (gate_owner_check(owner, id)) {
        advance_if_busy(owner);
        if (owner->state == mode_server_dungeon) return true;
        leave_error_mode(owner);
    }
    leave_ritual_or_dungeon_mode(owner);
    return false;
}

// SprjDungeonGateIns::STEP_Init_forChannelLoad: once the gate is idle in ChannelLoad, request 2.
extern "C" void bb_frame_timing_dungeon_gate_step_init_for_channel_load(DungeonGate *gate)
{
    if (!take_request_2(&gate->owner)) return;
    step_machine_count_step(gate);
    gate->vtable->method_38(gate);
}

// SprjDungeonGateIns::STEP_ModeSwitch: finish what the state is doing, then go to the first step
// of its mode.
extern "C" void bb_frame_timing_dungeon_gate_step_mode_switch(DungeonGate *gate)
{
    StateOwner *owner = &gate->owner;
    if (owner->busy != 0) {
        advance(owner);
        if (owner->busy != 0) return;
    }
    if (owner->state > mode_display_error) return;
    gate->vtable->set_step(gate, mode_first_step[owner->state]);
    gate->vtable->method_38(gate);
}

// Event 0 asks for request 1, then for request 2 (which sees the state request 1 left). Request
// 1's answer is stored and then overwritten: as in the original, only request 2's answer is left,
// but the first store is visible to the owner while request 2 advances it.
extern "C" void bb_frame_timing_dungeon_gate_on_event_0(DungeonGate *gate, const GateEvent *event, int32_t id)
{
    if (!is_gate_event(event, id, gate_event_0)) return;
    gate->request_taken = take_request_1(&gate->owner);
    gate->request_taken = take_request_2(&gate->owner);
}

// Event 1000: request 7 (only when idle in states 2..5).
extern "C" void bb_frame_timing_dungeon_gate_on_event_1000(DungeonGate *gate, const GateEvent *event, int32_t id)
{
    if (!is_gate_event(event, id, gate_event_1000)) return;
    gate->request_taken = take_request_7(&gate->owner);
}

// Event 3000: request 10 (only when idle). value_6a0 is reset after the request whether or not it
// was taken, as in the original.
extern "C" void bb_frame_timing_dungeon_gate_on_event_3000(DungeonGate *gate, const GateEvent *event, int32_t id)
{
    if (!is_gate_event(event, id, gate_event_3000)) return;
    gate->request_taken = take_request_10(&gate->owner);
    gate->value_6a0 = value_6a0_reset;
}

namespace {

// Advances the owner one fixed step if it is busy; true when it is still busy afterwards.
bool still_busy_after_step(StateOwner *owner)
{
    if (owner->busy == 0) return false;
    advance(owner);
    return owner->busy != 0;
}

// The current gate, advanced while busy: done when it is idle. While it stays busy these two
// report "not now" (4), where the mode checks below report "still busy" (1): that is what the
// originals return.
uint32_t holygrail_until_idle()
{
    return still_busy_after_step(&current_gate()->owner) ? result_not_now : result_done;
}

}  // namespace

extern "C" uint32_t bb_frame_timing_holygrail_until_idle_0200f970() { return holygrail_until_idle(); }
extern "C" uint32_t bb_frame_timing_holygrail_until_idle_020118f0() { return holygrail_until_idle(); }

// The current gate, advanced while busy; once idle, done in a dungeon mode, not now otherwise.
extern "C" uint32_t bb_frame_timing_holygrail_wait_for_dungeon_mode()
{
    StateOwner *owner = &current_gate()->owner;
    if (still_busy_after_step(owner)) return result_still_busy;
    gate_state_query(owner);  // the original asks and ignores the answer
    return in_dungeon_mode(owner->state) ? result_done : result_not_now;
}

// The same for the ritual modes (without the question).
extern "C" uint32_t bb_frame_timing_holygrail_wait_for_ritual_mode()
{
    StateOwner *owner = &current_gate()->owner;
    if (still_busy_after_step(owner)) return result_still_busy;
    return in_ritual_mode(owner->state) ? result_done : result_not_now;
}
