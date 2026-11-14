// SPDX-License-Identifier: GPL-2.0-or-later
// Character: queues a named animation event on a character's behavior graph.
//
// character_throw_event_01e19e20 (0x01e19e20; the character frame update calls it to fire a
// pending throw event) looks the event name up in the behavior manager's event-name table and,
// when the name is known (id 0 or more) and the character's behavior graph is there, appends an
// event {id, no payload, no sender} to the graph's event ring:
// - nothing at all happens while the manager-ready global is clear;
// - a missing behavior manager is reported by name (the singleton check) and read again;
// - the manager's table holder and its table are each checked before the lookup; the
//   character's behavior (+0x10) and graph (+0x18) only after it;
// - a full ring (count >= capacity, signed) is first grown to twice its capacity, or to 8 when
//   it has none; the write index wraps to 0 when it has reached the capacity.
// The event's four bytes after the id are padding the original fills from an uninitialised
// stack slot; the entry at the end of this file passes the same stack bytes on.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

// The event a behavior graph queues (24 bytes).
struct BehaviorEvent {
    int32_t id;
    uint32_t padding;
    void *payload;
    void *sender;
};
static_assert(sizeof(BehaviorEvent) == 0x18 && offsetof(BehaviorEvent, payload) == 0x8 &&
              offsetof(BehaviorEvent, sender) == 0x10);

// A ring of events: written at write_index, which wraps at the capacity.
struct EventRing {
    BehaviorEvent *events;
    int32_t capacity;
    uint8_t unknown_0x0c[0x4];
    int32_t write_index;
    int32_t count;
};
static_assert(offsetof(EventRing, capacity) == 0x8 && offsetof(EventRing, write_index) == 0x10 &&
              offsetof(EventRing, count) == 0x14);
constexpr int32_t ring_first_capacity = 8;
constexpr int32_t ring_growth_factor = 2;

struct BehaviorGraph {
    uint8_t unknown_0x00[0xb8];
    EventRing *event_ring;
};
static_assert(offsetof(BehaviorGraph, event_ring) == 0xb8);

struct Behavior {
    uint8_t unknown_0x00[0x18];
    BehaviorGraph *graph;
};
static_assert(offsetof(Behavior, graph) == 0x18);

struct AnimEvents {
    uint8_t unknown_0x00[0x10];
    Behavior *behavior;
};
static_assert(offsetof(AnimEvents, behavior) == 0x10);

struct EventNameTable;   // opaque: only passed to the lookup

struct EventNameHolder {
    EventNameTable *table;
};

struct BehaviorManager {
    uint8_t unknown_0x00[0x8];
    EventNameHolder *event_names;
};
static_assert(offsetof(BehaviorManager, event_names) == 0x8);

RT_GLOBAL(0x05940210, behavior_manager_ready, void *const);
RT_GLOBAL(0x059401f8, behavior_manager_instance, BehaviorManager *);
RT_GLOBAL(0x04d35c22, behavior_manager_name, const char);

// The event's id by name; negative when the name is not known.
RT_ORIGINAL(0x00d4a2c0, event_id_by_name, int32_t(EventNameTable *, const char *name));
// Grows the ring's storage to hold at least that many events.
RT_ORIGINAL(0x029bc380, event_ring_reserve, void(EventRing *, int32_t capacity));

void push_event(EventRing *ring, int32_t id, uint32_t stale_padding)
{
    if (ring->count >= ring->capacity)
        event_ring_reserve(ring, ring->capacity ? ring->capacity * ring_growth_factor : ring_first_capacity);
    int32_t index = ring->write_index;
    if (index == ring->capacity) index = 0;
    ring->write_index = index + 1;
    BehaviorEvent &event = ring->events[index];
    event.id = id;
    event.padding = stale_padding;
    event.payload = nullptr;
    event.sender = nullptr;
    ring->count++;
}

}  // namespace

// The body, entered from the hook below with the four stale stack bytes.
extern "C" __attribute__((visibility("hidden"), used)) void
character_throw_event_body(AnimEvents *events, const char *name, uint32_t stale_padding)
{
    if (!behavior_manager_ready.get()) return;
    BehaviorManager *manager = engine::require(behavior_manager_instance, behavior_manager_name.address());
    EventNameHolder *holder = manager->event_names;
    if (!holder || !holder->table) return;
    const int32_t id = event_id_by_name(holder->table, name);
    Behavior *behavior = events->behavior;
    if (!behavior || !behavior->graph || id < 0) return;
    push_event(behavior->graph->event_ring, id, stale_padding);
}

// Quirk: the original builds the event in its own stack frame and never writes the four bytes
// after the id (rbp - 0x3c): whatever the stack held there, 0x44 bytes below the caller's stack
// pointer, before the call. The entry reads them from there before anything else uses that
// stack (as game/render/model_wind.cpp does), so the queued event gets the same bytes.
#if defined(__APPLE__)
#define THROW_EVENT_BODY "_character_throw_event_body"
#else
#define THROW_EVENT_BODY "character_throw_event_body"
#endif
extern "C" __attribute__((naked, visibility("default"))) void bb_character_throw_event_01e19e20(AnimEvents *, const char *)
{
    asm volatile("movl -0x44(%rsp), %edx\n\t"
                 "jmp " THROW_EVENT_BODY);
}
