// SPDX-License-Identifier: GPL-2.0-or-later
// Six engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one virtual
// method that only vtables reach (no direct calls):
// - forwards_to_referenced_node_slot_0x28: resolve the node a reference names (a 16-bit kind and
//   a signed 16-bit index) in a context, and return what that node's slot 0x28 returns for the
//   same context, argument and flag byte (the slot the members themselves fill in their vtables).
// - posts_event_9_when_accepted: run the base handler, then, unless the event's flags have either
//   stop bit (read after the base handler), ask slot 0x78 whether the handler accepts the event
//   and, when it does (the answer is a byte), post event 9 with the source and the event.
// - syncs_selected_id_and_index: when the id slot holds an id other than the selected one, select
//   it and pass on the index of the first list entry with that id (-1 when none has it). The
//   members first call an empty function (a returns_nothing member), which this build leaves out.
// - selects_entry_id_by_index: unless the selector is locked, select the id of the entry at an
//   index below the list's count, store it in the id slot too, and pass the selection on.
// - releases_counted_reference_at_0x20: drop the strong and the weak count of the counted object
//   at +0x20 (destroying it when the strong count reaches zero while alive, freeing it when the
//   weak count does), clear +0x20 and +0x28 and pass on to the base release.
// - registers_new_entry_at_0x30: keep the owner at +0x28, make a 32-byte entry with the function
//   the global 0x056c45c8 points at, construct it when there is one, keep it at +0x30, give it
//   the value and register it with the owner (both read again after the value is set).
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

// forwards_to_referenced_node_slot_0x28
struct Node;
using NodeMethod = void *(Node *node, void *context, void *argument, uint8_t flag);
struct NodeVtable {
    uint8_t unknown_0x00[0x28];
    NodeMethod *method_0x28;
};
static_assert(offsetof(NodeVtable, method_0x28) == 0x28);
struct Node {
    const NodeVtable *vtable;
};
struct NodeReference {
    uint8_t unknown_0x00[0x8];
    uint16_t kind;
    int16_t index;
};
static_assert(offsetof(NodeReference, kind) == 0x8 && offsetof(NodeReference, index) == 0xa);

RT_ORIGINAL(0x02c4f560, resolve_node_reference, Node *(void *context, uint16_t kind, int16_t index));

// posts_event_9_when_accepted
struct Handler;
struct EventInfo;
using AcceptsEvent = uint8_t(Handler *handler, EventInfo *event);
struct HandlerVtable {
    uint8_t unknown_0x00[0x78];
    AcceptsEvent *accepts;
};
static_assert(offsetof(HandlerVtable, accepts) == 0x78);
struct Handler {
    const HandlerVtable *vtable;
};
// The event's flags: the members test the byte at +0x1b (the top byte of the dword at +0x18).
constexpr std::ptrdiff_t event_flags_high_byte = 0x1b;
constexpr uint8_t event_stop_bits = 0x90;
constexpr int32_t accepted_event_kind = 9;
constexpr uint64_t accepted_event_value = 0;

RT_ORIGINAL(0x0139c240, handle_event_base, void(Handler *, void *source, EventInfo *));
RT_ORIGINAL(0x0139bc30, post_handler_event, void(Handler *, int32_t kind, void *source, uint64_t value, EventInfo *));

// syncs_selected_id_and_index and selects_entry_id_by_index
struct IdEntry {
    uint8_t unknown_0x00[0x10];
    uint32_t id;
    uint8_t unknown_0x14[0x4];
};
static_assert(sizeof(IdEntry) == 0x18 && offsetof(IdEntry, id) == 0x10);
struct IdList {
    uint8_t unknown_0x00[0x18];
    IdEntry *begin;
    IdEntry *end;
};
static_assert(offsetof(IdList, begin) == 0x18 && offsetof(IdList, end) == 0x20);
struct IdSelector {
    uint8_t unknown_0x00[0xc];
    uint8_t flags;
    uint8_t unknown_0x0d[0x13];
    uint32_t id;
    uint32_t *id_slot;
    IdList *list;
};
static_assert(offsetof(IdSelector, flags) == 0xc && offsetof(IdSelector, id) == 0x20);
static_assert(offsetof(IdSelector, id_slot) == 0x28 && offsetof(IdSelector, list) == 0x30);
constexpr uint8_t selector_locked = 0x1;
constexpr int32_t no_index = -1;

RT_ORIGINAL(0x02752100, apply_selected_index, void(IdSelector *, int32_t index));
RT_ORIGINAL(0x02762360, apply_selected_id, void(IdSelector *));

// releases_counted_reference_at_0x20
struct Counted;
using CountedCall = void(Counted *);
struct CountedVtable {
    uint8_t unknown_0x00[0x18];
    CountedCall *destroy;
    CountedCall *free;
};
static_assert(offsetof(CountedVtable, destroy) == 0x18 && offsetof(CountedVtable, free) == 0x20);
// The counts: the strong count in bits 0-14, the weak count in bits 15-29, bit 30 alive.
struct Counted {
    const CountedVtable *vtable;
    uint32_t counts;
};
static_assert(offsetof(Counted, counts) == 0x8);
constexpr uint32_t count_mask = 0x7fff;
constexpr uint32_t weak_shift = 15;
constexpr uint32_t alive_bit = 0x40000000;
constexpr uint32_t above_strong = ~count_mask;
constexpr uint32_t keep_with_weak = 0xc0000000 | count_mask;
struct CountedHolder {
    uint8_t unknown_0x00[0x20];
    Counted *counted;
    uint64_t value_0x28;
};
static_assert(offsetof(CountedHolder, counted) == 0x20 && offsetof(CountedHolder, value_0x28) == 0x28);

RT_ORIGINAL(0x02759cc0, release_base, void(CountedHolder *));

void release_counted(Counted *counted)
{
    const uint32_t old = counted->counts;
    uint32_t counts = (old & above_strong) | ((old - 1) & count_mask);
    counted->counts = counts;
    if ((counts & count_mask) == 0 && (old & alive_bit)) {
        counted->vtable->destroy(counted);
        counts = counted->counts & ~alive_bit;
        counted->counts = counts;
    }
    const uint32_t weak = ((counts >> weak_shift) - 1) & count_mask;
    counted->counts = (counts & keep_with_weak) | (weak << weak_shift);
    if (weak == 0) {
        counted->counts = counts | (count_mask << weak_shift);
        counted->vtable->free(counted);
    }
}

// registers_new_entry_at_0x30
struct Entry {
    uint8_t unknown_0x00[0x20];
};
struct Registry;
struct Registrant {
    uint8_t unknown_0x00[0x28];
    Registry *registry;
    Entry *entry;
};
static_assert(offsetof(Registrant, registry) == 0x28 && offsetof(Registrant, entry) == 0x30);

using AllocateEntry = void *(uint64_t size);
RT_GLOBAL(0x056c45c8, allocate_entry, AllocateEntry *const);
RT_ORIGINAL(0x02aebf90, construct_entry, void(Entry *));
RT_ORIGINAL(0x02aebf60, set_entry_value, void(Entry *, void *value));
RT_ORIGINAL(0x02aeb740, register_entry, void(Registry *, Registrant *, Entry *));

}  // namespace

extern "C" void *bb_engine_forwards_to_referenced_node_slot_0x28(const NodeReference *reference, void *context,
                                                                 void *argument, uint8_t flag)
{
    Node *node = resolve_node_reference(context, reference->kind, reference->index);
    return node->vtable->method_0x28(node, context, argument, flag);
}

extern "C" void bb_engine_posts_event_9_when_accepted(Handler *handler, void *source, EventInfo *event)
{
    handle_event_base(handler, source, event);
    if (reinterpret_cast<const uint8_t *>(event)[event_flags_high_byte] & event_stop_bits)
        return;
    if (handler->vtable->accepts(handler, event) == 0)
        return;
    post_handler_event(handler, accepted_event_kind, source, accepted_event_value, event);
}

extern "C" void bb_engine_syncs_selected_id_and_index(IdSelector *selector)
{
    const uint32_t *slot = selector->id_slot;
    if (slot == nullptr || *slot == selector->id)
        return;
    const uint32_t id = *slot;
    selector->id = id;
    const IdEntry *begin = selector->list->begin;
    const uint32_t count = static_cast<uint32_t>(selector->list->end - begin);
    int32_t index = no_index;
    for (uint32_t i = 0; i < count; ++i) {
        if (begin[i].id == id) {
            index = static_cast<int32_t>(i);
            break;
        }
    }
    apply_selected_index(selector, index);
}

extern "C" void bb_engine_selects_entry_id_by_index(IdSelector *selector, uint32_t index)
{
    if (selector->flags & selector_locked)
        return;
    const IdList *list = selector->list;
    if (uint64_t{index} >= static_cast<uint64_t>(list->end - list->begin))
        return;
    const uint32_t id = list->begin[index].id;
    selector->id = id;
    *selector->id_slot = id;
    apply_selected_id(selector);
}

extern "C" void bb_engine_releases_counted_reference_at_0x20(CountedHolder *holder)
{
    if (Counted *counted = holder->counted) {
        release_counted(counted);
        holder->counted = nullptr;
    }
    holder->value_0x28 = 0;
    release_base(holder);
}

extern "C" void bb_engine_registers_new_entry_at_0x30(Registrant *self, void *value, Registry *registry)
{
    self->registry = registry;
    Entry *entry = static_cast<Entry *>(allocate_entry.get()(sizeof(Entry)));
    if (entry != nullptr)
        construct_entry(entry);
    self->entry = entry;
    set_entry_value(entry, value);
    register_entry(self->registry, self, self->entry);
}
