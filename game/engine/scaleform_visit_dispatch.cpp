// SPDX-License-Identifier: GPL-2.0-or-later
// Four families of scaleform functions (symbols/families.csv), two pairs that differ only in one
// member offset:
//  - dispatches_on_slot_match_with_member_0x68 / _0x70: compare the handle the third argument points
//    at against the two handle slots (+0x3b8, then +0x3b0) its value's owner chain reaches; on a
//    match call 0x005c9830 with the member at +0x68 (or +0x70), the four arguments and whether the
//    first slot matched; otherwise call 0x0056d2c0 with the four arguments. Values of kind 7 and up
//    compare the handle pointers; lower kinds compare the handles' targets, resolving the
//    argument's target through 0x004f0330 when it is still null.
//  - visits_base_pairs_and_entries_at_0x70 / _0x78: call 0x027fd2c0, then 0x027fe500 on the two
//    pairs at +0x70 and +0x88 (or +0x78 and +0x90), then visit every entry of the array the object
//    points at (+0xa0 or +0xa8) the way 0x027fe500 visits each of its two.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace {

// --- dispatches_on_slot_match_with_member_* -------------------------------------------------

struct Handle {
    uint8_t unknown_0x0[0x10];
    const void *target;  // null until 0x004f0330 resolves it
};
static_assert(offsetof(Handle, target) == 0x10);

struct HandleSlots {
    uint8_t unknown_0x0[0x3b0];
    Handle *second;
    Handle *first;
};
static_assert(offsetof(HandleSlots, second) == 0x3b0);
static_assert(offsetof(HandleSlots, first) == 0x3b8);

struct SlotContext {
    uint8_t unknown_0x0[0x18];
    HandleSlots *slots;
};
static_assert(offsetof(SlotContext, slots) == 0x18);

struct ValueOwner {
    uint8_t unknown_0x0[0x28];
    SlotContext *context;
};
static_assert(offsetof(ValueOwner, context) == 0x28);

struct Value {
    ValueOwner *owner;
    uint8_t kind;
};
static_assert(offsetof(Value, kind) == 8);

// Kinds from this one up are compared by handle pointer, lower kinds by the handles' targets.
constexpr uint8_t first_pointer_compared_kind = 7;

RT_ORIGINAL(0x004f0330, resolve_handle_target, void(Handle *handle));
RT_ORIGINAL(0x0056d2c0, dispatch_without_slot, bool(void *self, Value *value, Handle **handle, void *argument));
RT_ORIGINAL(0x005c9830, dispatch_with_slot,
            bool(void *member, void *self, Value *value, Handle **handle, void *argument, bool first_slot));

HandleSlots *value_slots(const Value *value)
{
    return value->owner->context->slots;
}

// The argument handle's target; a null target is resolved once and read again through the
// argument (the resolver may replace the handle it points at).
const void *argument_target(Handle **handle)
{
    Handle *current = *handle;
    const void *target = current->target;
    if (!target) {
        resolve_handle_target(current);
        target = (*handle)->target;
    }
    return target;
}

// The slot is read only after the argument's target is known: resolving may change it.
template <Handle *HandleSlots::*slot>
bool slot_matches(const Value *value, HandleSlots *slots, Handle **handle)
{
    if (value->kind >= first_pointer_compared_kind)
        return slots->*slot == *handle;
    const void *target = argument_target(handle);
    return (slots->*slot)->target == target;
}

template <std::ptrdiff_t member_offset>
bool dispatch_on_slot_match(uint8_t *self, Value *value, Handle **handle, void *argument)
{
    // The owner chain and the kind are read again for the second slot: resolving may change them.
    if (slot_matches<&HandleSlots::first>(value, value_slots(value), handle))
        return dispatch_with_slot(self + member_offset, self, value, handle, argument, true);
    if (slot_matches<&HandleSlots::second>(value, value_slots(value), handle))
        return dispatch_with_slot(self + member_offset, self, value, handle, argument, false);
    return dispatch_without_slot(self, value, handle, argument);
}

constexpr std::ptrdiff_t dispatch_member_0x68 = 0x68;
constexpr std::ptrdiff_t dispatch_member_0x70 = 0x70;

// --- visits_base_pairs_and_entries_at_* -----------------------------------------------------

struct VisitedEntry {
    uint8_t unknown_0x0[0x8];
    VisitedEntry *previous;
    VisitedEntry *next;
    uint32_t state;
};
static_assert(offsetof(VisitedEntry, previous) == 0x8);
static_assert(offsetof(VisitedEntry, next) == 0x10);
static_assert(offsetof(VisitedEntry, state) == 0x18);

struct VisitList {
    uint8_t unknown_0x0[0x8];
    VisitedEntry *current;
};
static_assert(offsetof(VisitList, current) == 0x8);

struct Visitor {
    uint8_t unknown_0x0[0x58];
    VisitList *list;
};
static_assert(offsetof(Visitor, list) == 0x58);

struct EntryArray {
    VisitedEntry **data;
    uint64_t count;
};
static_assert(offsetof(EntryArray, count) == 8);

constexpr uint32_t pending_state_bits = 0x70000000;
constexpr uint32_t listed_state_bit = 0x08000000;

RT_ORIGINAL(0x027fd2c0, visit_base, void(void *self, Visitor *visitor));
RT_ORIGINAL(0x027fe500, visit_pair, void(void *pair, Visitor *visitor));

// The links, the visitor's list and the entry array are pointers that any link store may
// overwrite (an entry's neighbour can be any memory), and the original reads each of them again
// after every store. These byte-wise accesses keep that: the compiler may not carry a typed
// pointer load across a store of another pointer type.
template <typename T>
T load_link(const void *at)
{
    T value;
    std::memcpy(&value, at, sizeof value);
    return value;
}

template <typename T>
void store_link(void *at, T value)
{
    std::memcpy(at, &value, sizeof value);
}

VisitList *visit_list(Visitor *visitor)
{
    return load_link<VisitList *>(&visitor->list);
}

VisitedEntry *current_entry(Visitor *visitor)
{
    return load_link<VisitedEntry *>(&visit_list(visitor)->current);
}

// One entry, as 0x027fe500 visits each of its two: count the visit; when any pending bit was set,
// clear them, and when the entry is also listed, move it from its place in the list to right
// after the visitor's current entry, which it then becomes.
void visit_entry(VisitedEntry *entry, Visitor *visitor)
{
    const uint32_t state = entry->state + 1;
    entry->state = state;
    if (!(state & pending_state_bits))
        return;
    entry->state = state & ~pending_state_bits;
    if (!(state & listed_state_bit))
        return;
    store_link(&load_link<VisitedEntry *>(&entry->next)->previous, load_link<VisitedEntry *>(&entry->previous));
    store_link(&load_link<VisitedEntry *>(&entry->previous)->next, load_link<VisitedEntry *>(&entry->next));
    store_link(&entry->next, load_link<VisitedEntry *>(&current_entry(visitor)->next));
    store_link(&entry->previous, current_entry(visitor));
    store_link(&current_entry(visitor)->next, entry);
    store_link(&visit_list(visitor)->current, entry);
}

template <std::ptrdiff_t first_pair>
void visit_base_pairs_and_entries(uint8_t *self, Visitor *visitor)
{
    constexpr std::ptrdiff_t second_pair = first_pair + 0x18;
    constexpr std::ptrdiff_t entries = first_pair + 0x30;
    auto entry_array = [self] { return load_link<EntryArray *>(self + entries); };

    visit_base(self, visitor);
    visit_pair(self + first_pair, visitor);
    visit_pair(self + second_pair, visitor);
    // The array pointer and its data are read again for every entry (a visit may change them);
    // the count is read once.
    const EntryArray *array = entry_array();
    if (!array)
        return;
    const uint64_t count = load_link<uint64_t>(&array->count);
    for (uint64_t index = 0; index < count; ++index) {
        VisitedEntry **data = load_link<VisitedEntry **>(&entry_array()->data);
        VisitedEntry *entry = load_link<VisitedEntry *>(&data[index]);
        if (entry)
            visit_entry(entry, visitor);
    }
}

constexpr std::ptrdiff_t first_pair_0x70 = 0x70;
constexpr std::ptrdiff_t first_pair_0x78 = 0x78;

}  // namespace

extern "C" bool bb_engine_dispatches_on_slot_match_with_member_0x68(uint8_t *self, Value *value, Handle **handle,
                                                                    void *argument)
{
    return dispatch_on_slot_match<dispatch_member_0x68>(self, value, handle, argument);
}

extern "C" bool bb_engine_dispatches_on_slot_match_with_member_0x70(uint8_t *self, Value *value, Handle **handle,
                                                                    void *argument)
{
    return dispatch_on_slot_match<dispatch_member_0x70>(self, value, handle, argument);
}

extern "C" void bb_engine_visits_base_pairs_and_entries_at_0x70(uint8_t *self, Visitor *visitor)
{
    visit_base_pairs_and_entries<first_pair_0x70>(self, visitor);
}

extern "C" void bb_engine_visits_base_pairs_and_entries_at_0x78(uint8_t *self, Visitor *visitor)
{
    visit_base_pairs_and_entries<first_pair_0x78>(self, visitor);
}
