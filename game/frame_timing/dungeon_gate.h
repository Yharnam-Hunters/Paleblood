// SPDX-License-Identifier: GPL-2.0-or-later
// The Chalice Dungeon gate (SprjDungeonGateIns): its state, the handle the state holds, and the
// parts of FrpgNetMan the gate's online methods use. The gate's replacements are spread over
// fixed_step_states.cpp, fixed_step_helpers.cpp, creation_helpers.cpp, handle_pass_on.cpp,
// give_handle_to_request.cpp and the two pass-on files; each declares here what more than one of
// them reads.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "frame_time.h"
#include "runtime/original.h"

namespace frame_timing::dungeon_gate {

// ---- The gate and its state ----------------------------------------------------------------------

// The gate's modes, as STEP_ModeSwitch reads them from the owner's state. The ritual and dungeon
// modes come in pairs: local (even) and server (the next, odd).
enum GateMode : uint32_t {
    mode_channel_load = 0,
    mode_no_holygrail = 1,
    mode_local_ritual = 2,
    mode_server_ritual = 3,
    mode_local_dungeon = 4,
    mode_server_dungeon = 5,
    mode_display_error = 6,
};

struct StateOwner;
struct StateOwnerVtable {
    void *unknown_slots[26];
    void (*advance)(StateOwner *, FrameTime *);
};
static_assert(offsetof(StateOwnerVtable, advance) == 0xd0);

constexpr uint32_t slot_count = 16;  // the owner's slots, which the ritual slot reset lists

// The gate's state (SprjDungeonGateIns +0x108).
struct StateOwner {
    const StateOwnerVtable *vtable;
    uint8_t unknown_0x008[0xc8];
    engine::RefObject *handle;  // a GateHandle (as_gate_handle), or null
    uint8_t unknown_0x0d8[0xb0];
    struct Slot {
        uint32_t key;
        uint32_t active;  // a slot counts when nonzero
        uint8_t unknown_0x008[0x4];
    } slots[slot_count];  // what the ritual slot reset (0x01ea5850) collects
    uint8_t unknown_0x248[0x14];
    uint32_t state;  // a GateMode
    uint32_t request;
    int32_t busy;  // nonzero while the state is advancing
    uint8_t unknown_0x268[0x4];
    float value_26c;  // set to 10 with request 2; its meaning is not known
};
static_assert(offsetof(StateOwner, handle) == 0xd0 && offsetof(StateOwner, slots) == 0x188 &&
              sizeof(StateOwner::Slot) == 12);
static_assert(offsetof(StateOwner, state) == 0x25c && offsetof(StateOwner, request) == 0x260 &&
              offsetof(StateOwner, busy) == 0x264 && offsetof(StateOwner, value_26c) == 0x26c);

struct DungeonGate;
struct DungeonGateVtable {
    void *unknown_slots[5];
    void (*set_step)(DungeonGate *, int32_t step);  // an index into the step table
    void *unknown_slot_30;
    void (*method_38)(DungeonGate *);  // called after STEP_Init_forChannelLoad and a mode switch
};
static_assert(offsetof(DungeonGateVtable, set_step) == 0x28 && offsetof(DungeonGateVtable, method_38) == 0x38);

// SprjDungeonGateIns: a step machine (its base keeps the current step and counts steps) with the
// state at +0x108.
struct DungeonGate {
    const DungeonGateVtable *vtable;
    uint8_t unknown_0x008[0x48];
    uint32_t step;           // the index of the current step
    uint32_t steps_counted;  // what reads it is not known
    uint8_t unknown_0x058[0xb0];
    StateOwner owner;
    bool request_taken;  // what the last event handler's request did
    uint8_t unknown_0x379[0x327];
    int32_t value_6a0;  // reset by the handler of event 3000
};
static_assert(offsetof(DungeonGate, step) == 0x50 && offsetof(DungeonGate, steps_counted) == 0x54 &&
              offsetof(DungeonGate, owner) == 0x108 && offsetof(DungeonGate, request_taken) == 0x378 &&
              offsetof(DungeonGate, value_6a0) == 0x6a0);

// ---- The gate's handle ---------------------------------------------------------------------------

// One item of a slot item list: the ritual slot reset fills it with a slot's key, its index and the
// caller's value for that index.
struct SlotItem {
    uint32_t key;
    uint32_t index;
    uint32_t value;
};
static_assert(sizeof(SlotItem) == 12);

// The engine's growable list of slot items (its first 8 bytes are not read by the gate's code).
struct SlotItemList {
    uint64_t unknown_0x000;
    SlotItem *begin;
    SlotItem *end;
    SlotItem *capacity;
    engine::Allocator *heap;
};
static_assert(offsetof(SlotItemList, begin) == 0x8 && offsetof(SlotItemList, heap) == 0x20);

// The handle's items, 16 bytes apart, of which passing the handle on copies the first 12.
struct HandleItem {
    SlotItem item;
    uint8_t unknown_0x00c[0x4];
};
static_assert(sizeof(HandleItem) == 0x10);

// The id the owner check is given (its first 64 bits; INT64_MIN when there is none). The handles
// carry one at +0x10: the lookup finds a handle by it, and the level creation builds its handle
// with the date stamp as the id. That it names an online session is a guess from FrpgNetMan's part.
struct SessionId {
    int64_t value;
};
constexpr int64_t no_session_id = INT64_MIN;

// The object the gate's state holds as its handle (StateOwner::handle): reference counted, 0x130
// bytes from the heap at 0x05940438, built by 0x02267200 with an id, kept in the list of
// FrpgNetMan's handle service. The meanings of its words are not known; each comment says who
// reads or writes it.
struct GateHandle {
    engine::RefObject base;
    int64_t id;  // the id it was built with (SessionId)
    uint8_t unknown_0x018[0x38];
    uint32_t word_050;  // must be zero for the readiness check (server modes) and to pass the handle on
    uint8_t unknown_0x054[0x1c];
    HandleItem *items_begin;  // copied into a slot item list when the handle is passed on
    HandleItem *items_end;
    uint8_t unknown_0x080[0x8c];
    uint8_t flags_10c;  // bit 0 (handle_blocks_reset) stops the reset in the server ritual mode
    uint8_t unknown_0x10d[0x3];
    uint32_t word_110;  // set to 1 by the level creation
    uint32_t word_114;  // likewise
    uint8_t unknown_0x118[0x4];
    uint32_t value_11c;  // cleared before the handle is passed on; set by the local pass-on paths
    uint32_t value_120;  // cleared before the handle is passed on; set by both pass-on paths
    uint32_t value_124;  // cleared by the ritual resets
    uint8_t unknown_0x128[0x8];
};
static_assert(offsetof(GateHandle, id) == 0x10 && offsetof(GateHandle, word_050) == 0x50 &&
              offsetof(GateHandle, items_begin) == 0x70 && offsetof(GateHandle, items_end) == 0x78);
static_assert(offsetof(GateHandle, flags_10c) == 0x10c && offsetof(GateHandle, word_110) == 0x110 &&
              offsetof(GateHandle, word_114) == 0x114 && offsetof(GateHandle, value_11c) == 0x11c &&
              offsetof(GateHandle, value_120) == 0x120 && offsetof(GateHandle, value_124) == 0x124 &&
              sizeof(GateHandle) == 0x130);
constexpr uint8_t handle_blocks_reset = 1;
constexpr uint64_t gate_handle_alignment = 8;
RT_GLOBAL(0x05940438, gate_handle_heap, engine::Allocator *);

inline GateHandle *as_gate_handle(engine::RefObject *handle) { return reinterpret_cast<GateHandle *>(handle); }
inline const GateHandle *as_gate_handle(const engine::RefObject *handle)
{
    return reinterpret_cast<const GateHandle *>(handle);
}

// A vector of handle pointers (its first 8 bytes are not read by the gate's code).
struct GateHandleList {
    uint8_t unknown_0x00[0x8];
    GateHandle **begin;
    GateHandle **end;
    GateHandle **capacity;
};
static_assert(offsetof(GateHandleList, begin) == 0x8 && offsetof(GateHandleList, end) == 0x10 &&
              offsetof(GateHandleList, capacity) == 0x18);

// Builds a handle for the id in the memory (not replaced): it keeps the id at +0x10 and zeroes its
// count.
RT_ORIGINAL(0x02267200, construct_gate_handle, void(GateHandle *handle, const SessionId *id));
// Appends the handle pointer to the list (not replaced).
RT_ORIGINAL(0x02266b50, append_gate_handle, void(GateHandleList *list, GateHandle *const *handle));
// Appends a copy of an item to the list (0x01e9d960; handle_pass_on.cpp replaces it).
RT_ORIGINAL(0x01e9d960, append_slot_item, void(SlotItemList *list, const SlotItem *item));

// ---- FrpgNetMan ----------------------------------------------------------------------------------

// FrpgNetMan's object at +0xca8, called a handle service here: it keeps the list of gate handles at
// +0x50 (the lookup searches it; the level creation appends to it) and makes the requests that
// carry a handle (0x02264640). Its class is not known; the name says what both known uses have in
// common, where "session" (from SessionId) would be a guess and "pass on" covers only one use.
struct HandleService {
    uint8_t unknown_0x00[0x50];
    GateHandleList handles;
};
static_assert(offsetof(HandleService, handles) == 0x50);

// What the service makes (0x02264640, 0xe0 bytes from the heap at 0x05940420): a request the gate
// gives a handle to pass on (0x02269e30, 0x02269d40, which then send it on through its vtable), an
// id's handle (0x02269870) or a new handle and its levels (0x0226a130).
struct HandleRequest;
struct HandleRequestVtable {
    void *unknown_0x00[5];
    void (*send)(HandleRequest *request, int32_t code);
    void *unknown_0x30;
    void (*prepare)(HandleRequest *request);  // called before the local send; its purpose is not known
};
static_assert(offsetof(HandleRequestVtable, send) == 0x28 && offsetof(HandleRequestVtable, prepare) == 0x38);
struct HandleRequest {
    const HandleRequestVtable *vtable;
    uint8_t unknown_0x08[0xc0];
    engine::RefObject *handle;
    uint8_t unknown_0xd0[0xc];
    int32_t kind;
};
static_assert(offsetof(HandleRequest, handle) == 0xc8 && offsetof(HandleRequest, kind) == 0xdc &&
              sizeof(HandleRequest) == 0xe0);

// Makes a request from the service (not replaced; may answer null when its allocation fails).
RT_ORIGINAL(0x02264640, make_handle_request, HandleRequest *(HandleService *service));

// The block of FrpgNetMan's at +0xa68 that the handle creation (0x01ea47a0) passes on by address.
// Nothing here reads it; its extent is not known (the space up to the handle service is reserved).
struct NetCreationBlock {
    uint8_t unknown_0x000[0x240];
};

// FrpgNetMan, the online manager: the four bytes the gate's online checks test (their meaning is
// not known), the block the handle creation passes on, and the handle service.
struct FrpgNetMan {
    uint8_t unknown_0x000[0x8];
    uint8_t flag_008;  // must be nonzero
    uint8_t unknown_0x009[0x9ed];
    uint8_t flag_9f6;  // must be zero
    uint8_t unknown_0x9f7[0x1];
    uint8_t flag_9f8;  // must be zero
    uint8_t unknown_0x9f9[0x57];
    uint8_t flag_a50;  // must be zero
    uint8_t unknown_0xa51[0x17];
    NetCreationBlock creation_block;
    HandleService *handle_service;
};
static_assert(offsetof(FrpgNetMan, flag_008) == 0x8 && offsetof(FrpgNetMan, flag_9f6) == 0x9f6 &&
              offsetof(FrpgNetMan, flag_9f8) == 0x9f8 && offsetof(FrpgNetMan, flag_a50) == 0xa50 &&
              offsetof(FrpgNetMan, creation_block) == 0xa68 &&
              offsetof(FrpgNetMan, handle_service) == 0xca8);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// FrpgNetMan, as the game's singleton check reads it: a missing one is reported and the singleton
// read again (engine::require).
inline FrpgNetMan *require_frpg_net_man()
{
    return engine::require(frpg_net_man_instance, frpg_net_man_name.address());
}

// FrpgNetMan's four bytes, as the gate's online checks test them, in the original's order
// ("online" is a guess from FrpgNetMan's part).
inline bool net_allows(const FrpgNetMan *net)
{
    return net->flag_008 != 0 && net->flag_9f8 == 0 && net->flag_9f6 == 0 && net->flag_a50 == 0;
}

// The same, with FrpgNetMan read through the singleton check first.
inline bool net_allows() { return net_allows(require_frpg_net_man()); }

// Whether the gate's state allows a ritual reset (0x01ea5570, and the opening test of the two
// ritual resets 0x01ea5670 and 0x01ea5850): idle in the local ritual mode, or idle in the server
// ritual mode with FrpgNetMan's bytes allowing it and no handle or one whose flag bit 0 is clear.
// The original checks FrpgNetMan again with the pointer the bytes were just read through before it
// reads the handle, so that check cannot fail and is left out.
inline bool ritual_reset_allowed(const StateOwner *owner)
{
    if (owner->busy != 0) return false;
    if (owner->state == mode_local_ritual) return true;
    if (owner->state != mode_server_ritual || !net_allows()) return false;
    const GateHandle *handle = as_gate_handle(owner->handle);
    return !handle || (handle->flags_10c & handle_blocks_reset) == 0;
}

// ---- The handle creations' inputs ----------------------------------------------------------------

// The record a creation source points at: a number at +0x18, used as it is when not negative, and
// the two values a negative one is replaced with. What they stand for is not known.
struct CreationRecord {
    uint8_t unknown_0x000[0x2];
    uint16_t value_002;
    uint8_t unknown_0x004[0x1];
    uint8_t count_005;
    uint8_t unknown_0x006[0x12];
    int32_t number;
};
static_assert(offsetof(CreationRecord, value_002) == 0x2 && offsetof(CreationRecord, count_005) == 0x5 &&
              offsetof(CreationRecord, number) == 0x18);

// What the handle creation (0x01ea47a0) is given; it passes it, and its word at +0x10 as the limit,
// to 0x0231fbc0. What the object is, is not known.
struct CreationSource {
    uint8_t unknown_0x000[0x8];
    const CreationRecord *record;  // may be null
    uint32_t value_010;            // the limit 0x0231fbc0 is given
};
static_assert(offsetof(CreationSource, record) == 0x8 && offsetof(CreationSource, value_010) == 0x10);

// The first block the handle creation fills on its stack (0x0231fbc0): one 32-bit word; the
// creation's slot is 8 bytes (up to the created object's) and nothing past the word is written.
struct CreationPart1 {
    uint32_t packed;
    uint8_t unknown_0x004[0x4];
};
static_assert(sizeof(CreationPart1) == 0x8);

// The four flag levels the level creation (0x01ea4cb0) reads from event flags and passes on to
// 0x02265580 as they are. What they stand for is not known.
constexpr uint32_t level_columns = 4;
struct FlagLevels {
    int32_t level[level_columns];
};

}  // namespace frame_timing::dungeon_gate
