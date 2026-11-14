// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: two steps the task parts call on their items and listeners (task_parts.cpp).
//
// - frame_timing_item_update_013d4810 (the item list's update, once per item): first brings the
//   item's group up to date (each of the group's items takes its requested enabled state). Then,
//   when the item's own requested state differs from its enabled state, the item takes it and is
//   switched on or off accordingly; when they agree, an enabled item is switched on again and a
//   disabled one is left alone. The two states are whole bytes, compared as such.
// - frame_timing_listener_list_prepare_013dab20 (the listener list's notify, before the
//   listeners are called): with an owner, hands it to every listener; then, when the named
//   resource registry exists and holds a resource under the list's name, hands that resource to
//   every listener. Each pass takes the listener count once, as the 32-bit low half of the
//   vector's length, and reads the vector (and the owner) again for every listener.
//
// task_parts.cpp keeps its own partial ItemList and ListenerList, so that file and its
// verification rows stay as they are.
#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"
#include "../menu/scaleform_glue.h"
#include "runtime/original.h"

namespace {

// ---- The item ------------------------------------------------------------------------------

struct ItemContext;   // only passed on
struct ItemGroup;     // only passed on

struct Item {
    uint8_t unknown_0x00[0x28];
    ItemContext *context;
    ItemGroup *group;
    uint8_t unknown_0x38[0x30];
    uint8_t enabled;
    uint8_t enable_requested;
};
static_assert(offsetof(Item, context) == 0x28 && offsetof(Item, group) == 0x30);
static_assert(offsetof(Item, enabled) == 0x68 && offsetof(Item, enable_requested) == 0x69);

// Every item of the group takes its requested enabled state.
RT_ORIGINAL(0x0144d860, group_apply_requests, void(ItemGroup *));
// Switches an item's part of the group on or off.
RT_ORIGINAL(0x013d58a0, item_switch_on, void(ItemContext *, ItemGroup *));
RT_ORIGINAL(0x013d57a0, item_switch_off, void(ItemContext *, ItemGroup *));

// ---- The listener list -----------------------------------------------------------------------

struct Listener;      // only passed on
struct ListenerOwner; // only passed on

struct ListenerList {
    uint8_t unknown_0x00[0x20];
    Listener **begin;
    Listener **end;
    uint8_t unknown_0x30[0x10];
    ListenerOwner *owner;
    engine::WideString name;
};
static_assert(offsetof(ListenerList, begin) == 0x20 && offsetof(ListenerList, end) == 0x28);
static_assert(offsetof(ListenerList, owner) == 0x40 && offsetof(ListenerList, name) == 0x48);

// Each takes what it is given only once (and only something non-null).
RT_ORIGINAL(0x013d6880, listener_take_owner, void(Listener *, ListenerOwner *));
RT_ORIGINAL(0x013d68b0, listener_take_resource, void(Listener *, void *resource));

constexpr unsigned pointer_size_shift = 3;

// The listener count as the original takes it: the vector's byte length shifted down, low 32 bits.
uint32_t listener_count(const ListenerList *list)
{
    const uintptr_t bytes = reinterpret_cast<uintptr_t>(list->end) - reinterpret_cast<uintptr_t>(list->begin);
    return static_cast<uint32_t>(bytes >> pointer_size_shift);
}

const char16_t *name_text(const engine::WideString &name)
{
    return name.capacity >= engine::wide_string_inline_capacity ? name.heap : name.inline_text;
}

}  // namespace

extern "C" void bb_frame_timing_item_update_013d4810(Item *item)
{
    group_apply_requests(item->group);
    const uint8_t enabled = item->enabled;   // read after the group's update
    const uint8_t requested = item->enable_requested;
    if (enabled != requested) {
        item->enabled = requested;
        if (!requested) {
            item_switch_off(item->context, item->group);
            return;
        }
    } else if (!enabled) {
        return;
    }
    item_switch_on(item->context, item->group);
}

extern "C" void bb_frame_timing_listener_list_prepare_013dab20(ListenerList *list)
{
    if (list->owner) {
        const uint32_t count = listener_count(list);
        for (uint32_t i = 0; i != count; i++) listener_take_owner(list->begin[i], list->owner);
    }
    menu::scaleform::NamedResourceRegistry *registry = menu::scaleform::named_resource_registry.get();
    if (!registry) return;
    void *resource = menu::scaleform::registry_find(registry, name_text(list->name));
    if (!resource) return;
    const uint32_t count = listener_count(list);
    for (uint32_t i = 0; i != count; i++) listener_take_resource(list->begin[i], resource);
}
