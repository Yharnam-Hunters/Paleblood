// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: passing the Chalice Dungeon gate's handle on, and the growable list of slot items
// it builds for that (the ritual resets in fixed_step_states.cpp call the first two).
// - frame_timing_pass_handle_on_01e9cb80: with no handle, reports false. Otherwise (after the
//   FrpgNetMan check, which reports a missing one and reads the handle again) it copies the
//   handle object's items into a new list on the item heap, hands the handle's address and that
//   list to 0x022658b0, frees the list's block and reports true.
// - frame_timing_pass_handle_on_with_items_01e9cd20: the same, but it hands on the caller's list
//   instead; the list it builds from the handle object is only freed.
// - frame_timing_append_slot_item_01e9d960: appends a copy of an item (the list's push_back). An
//   item inside the list is found again by its index after the list grows.
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "runtime/original.h"

namespace {

// One item of the list: the ritual slot reset fills it with a slot's key, its index and the
// caller's value for that index.
struct SlotItem {
    uint32_t key;
    uint32_t index;
    uint32_t value;
};
static_assert(sizeof(SlotItem) == 12);

// The engine's growable list of slot items (its first 8 bytes are not read here).
struct SlotItemList {
    uint64_t unknown_0x000;
    SlotItem *begin;
    SlotItem *end;
    SlotItem *capacity;
    engine::Allocator *heap;
};
static_assert(offsetof(SlotItemList, begin) == 0x8 && offsetof(SlotItemList, heap) == 0x20);

// The largest count of items the list's size arithmetic allows; a full list grows by a half.
constexpr size_t max_slot_items = std::numeric_limits<size_t>::max() / sizeof(SlotItem);
constexpr size_t growth_divisor = 2;

// Moves the list to a block for `capacity` items from its heap (0x01e9da70, not replaced).
RT_ORIGINAL(0x01e9da70, grow_slot_items, void(SlotItemList *list, size_t capacity));
RT_ORIGINAL(0x01e9d960, append_slot_item, void(SlotItemList *list, const SlotItem *item));

// The handle object as passing it on reads it: its items, 16 bytes apart, of which the first 12
// are copied.
struct HandleItem {
    SlotItem item;
    uint8_t unknown_0x00c[0x4];
};
static_assert(sizeof(HandleItem) == 0x10);
struct HandleObject {
    engine::RefObject base;
    uint8_t unknown_0x010[0x60];
    HandleItem *items_begin;
    HandleItem *items_end;
};
static_assert(offsetof(HandleObject, items_begin) == 0x70 && offsetof(HandleObject, items_end) == 0x78);

struct FrpgNetMan;
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// The heap the list is built on.
RT_GLOBAL(0x05940428, item_list_heap, engine::Allocator *);
RT_GLOBAL(0x04b37dc0, message_incompatible_heap, const char);
constexpr int32_t line_incompatible_heap = 0x3e;

// Takes the handle's address and a list; it goes through FrpgNetMan (0x022658b0, not replaced;
// its result is ignored).
RT_ORIGINAL(0x022658b0, pass_items_on, void(engine::RefObject **handle, SlotItemList *list));

// The capacity a full list grows to: half as much again, or one more when that is not possible.
size_t grown_capacity(const SlotItemList *list)
{
    const size_t size = static_cast<size_t>(list->end - list->begin);
    const size_t growth = size / growth_divisor;
    size_t capacity = max_slot_items - growth < size ? 0 : size + growth;
    if (capacity < size + 1) capacity = size + 1;
    return capacity;
}

// Both pass-on functions: lists the handle object's items and hands the handle's address on with
// `items` (the new list when null). The block freed is the one the new list held before the call,
// through the list's heap as it is after the call.
bool pass_handle_on(engine::RefObject **handle, SlotItemList *items)
{
    engine::RefObject *object = *handle;
    if (!object) return false;
    if (!frpg_net_man_instance.get()) {
        engine::report_missing(frpg_net_man_name.address());
        object = *handle;
        if (!object) return false;
    }
    engine::Allocator *const heap = item_list_heap.get();
    engine::AllocatorInfo info;
    heap->vtable->info(&info, heap, 0);
    if ((info.flags & engine::allocator_info_valid) == 0)
        engine::fatal_error(nullptr, line_incompatible_heap, message_incompatible_heap.address());

    SlotItemList list{};
    list.heap = heap;
    const auto *source = reinterpret_cast<const HandleObject *>(object);
    const size_t count = static_cast<size_t>(source->items_end - source->items_begin);
    for (size_t index = 0; index != count; ++index) {
        const SlotItem item = source->items_begin[index].item;
        append_slot_item(&list, &item);
    }
    SlotItem *const block = list.begin;
    pass_items_on(handle, items ? items : &list);
    if (block) list.heap->free(block);
    return true;
}

}  // namespace

extern "C" bool bb_frame_timing_pass_handle_on_01e9cb80(engine::RefObject **handle)
{
    return pass_handle_on(handle, nullptr);
}

extern "C" bool bb_frame_timing_pass_handle_on_with_items_01e9cd20(engine::RefObject **handle, SlotItemList *items)
{
    return pass_handle_on(handle, items);
}

extern "C" void bb_frame_timing_append_slot_item_01e9d960(SlotItemList *list, const SlotItem *item)
{
    SlotItem *const old_begin = list->begin;
    if (item < list->end && old_begin <= item) {
        // The item is in the list: copy it from where its index lands after any growth.
        const ptrdiff_t index = item - old_begin;
        if (list->end == list->capacity) grow_slot_items(list, grown_capacity(list));
        if (list->end) *list->end = list->begin[index];
    } else {
        if (list->end == list->capacity) grow_slot_items(list, grown_capacity(list));
        if (list->end) *list->end = *item;
    }
    ++list->end;
}
