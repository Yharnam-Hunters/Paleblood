// SPDX-License-Identifier: GPL-2.0-or-later
// Render: three callees of the frame update and the swap-chain resource setup, and the two calls of
// the notify request's parameter set.
//
// - render_list_prepare_02379d60 (the frame update's prepare call, frame_update.cpp): the engine
//   list's splice. It moves the nodes [first, last) of another list in front of `where` in this
//   list. When both lists' allocators report the same heap identity the nodes are relinked as
//   they are (the sizes move by `count` unless the two lists are one; a size that would pass the
//   list's maximum throws the engine's length error). Otherwise each value is inserted here as a
//   new node and the moved range is erased from the other list: a range covering the whole list
//   empties it first and then frees every node, anything else unlinks and frees node by node,
//   never the other list's sentinel.
// - render_object_update_02373850 (each object of the frame update's list): refreshes the
//   object's visible flag, then runs its little life cycle. The flag comes from its owner's
//   query slot when the object says so, else from the object's two masks ANDed with the masks of
//   the owner's view (or the render manager's view, or the defaults when it has none) and
//   compared with the empty masks: any difference means visible. A flag that changed is stored
//   with the changed bit. Then, by state: idle objects start (a finish request or a notify
//   request skips the start and finishes them; a start that reports success makes them run);
//   running objects update with the step unless a finish was requested, and a notify request
//   hands them a one-entry parameter set; the changed bits are cleared after it. A finishing
//   object finishes and is done. It returns false once the object is done (the caller drops it).
// - render_copy_initial_level_014a3410 (swap_chain_resource_info.h): copies one level's initial
//   data through the box copy: the whole level from the origin, its width and height in
//   texels, or in 4x4 blocks for block-compressed formats and levels flagged as blocked. A null
//   level is refused with the unsupported status without copying.
// - render_set_parameter_02c51f60: sets the value of a key of a parameter set. The keys are sorted
//   (the set's creation sorts them), so it finds the first key not below the given one by binary
//   search, as std::lower_bound does, and stores the value beside it only when that key is the one
//   given; an unknown key changes nothing.
// - render_clear_parameter_set_02c51fc0: frees the set's block of keys and values through the
//   set's allocator (its free, slot 0x70) and clears the pointer, when there is one. The values
//   pointer and the count are left as they were.
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "record_geometry.h"
#include "runtime/original.h"

namespace {

// ---- The engine list ------------------------------------------------------------------------

struct ListNode {
    ListNode *next;
    ListNode *prev;
    void *value;
};
static_assert(offsetof(ListNode, value) == 0x10);

// The list's allocator: its third slot reports which heap it uses (lists on one heap can share
// nodes), its last one frees a node.
struct ListAllocator;
struct ListAllocatorVtable {
    void *unknown_slots[2];
    uint32_t (*heap_identity)(ListAllocator *);
    void *unknown_slots_1[11];
    void (*free)(ListAllocator *, ListNode *);
};
static_assert(offsetof(ListAllocatorVtable, heap_identity) == 0x10 && offsetof(ListAllocatorVtable, free) == 0x70);
struct ListAllocator {
    const ListAllocatorVtable *vtable;
};

struct List {
    void *proxy;
    ListNode *sentinel;
    uint64_t size;
    ListAllocator *allocator;
};
static_assert(offsetof(List, size) == 0x10 && offsetof(List, allocator) == 0x18);

constexpr uint64_t list_max_size = 0x0aaaaaaaaaaaaaa9;

RT_ORIGINAL(0x02379f00, insert_value, void(List *, ListNode *where, void *const *value));
RT_ORIGINAL(0x02fbf1b8, throw_length_error, void(const char *message));
RT_GLOBAL(0x04d40378, message_list_too_long, const char);

[[noreturn]] void list_too_long()
{
    throw_length_error(message_list_too_long.address());
    __builtin_unreachable();
}

// Moves [first, last) in front of `where`, in the game's order of link updates.
void relink(ListNode *where, ListNode *first, ListNode *last)
{
    first->prev->next = last;
    last->prev->next = where;
    where->prev->next = first;
    ListNode *const where_prev = where->prev;
    where->prev = last->prev;
    last->prev = first->prev;
    first->prev = where_prev;
}

// Erases [first, last) from `list`, freeing the nodes (their values need no destruction).
void erase(List *list, ListNode *first, ListNode *last)
{
    ListNode *const sentinel = list->sentinel;
    if (sentinel->next == first && last == sentinel) {
        sentinel->next = sentinel;
        list->sentinel->prev = list->sentinel;
        list->size = 0;
        for (ListNode *node = first; node != list->sentinel;) {
            ListNode *const next = node->next;
            list->allocator->vtable->free(list->allocator, node);
            node = next;
        }
        return;
    }
    for (ListNode *node = first; node != last;) {
        ListNode *const next = node->next;
        if (node != list->sentinel) {
            node->prev->next = next;
            node->next->prev = node->prev;
            list->allocator->vtable->free(list->allocator, node);
            list->size--;
        }
        node = next;
    }
}

// ---- The updated object -----------------------------------------------------------------------

// Four 32-bit lanes of each of two masks, side by side.
constexpr int mask_lanes = 4;
struct MaskPair {
    uint32_t first[mask_lanes];
    uint32_t second[mask_lanes];
};
static_assert(sizeof(MaskPair) == 0x20);

// A parameter set: its allocator and one block holding the sorted keys, then their values.
struct ParameterSet {
    engine::Allocator *allocator;
    uint32_t *keys;
    uint32_t *values;
    uint32_t count;
};
static_assert(offsetof(ParameterSet, keys) == 0x8 && offsetof(ParameterSet, values) == 0x10);
static_assert(offsetof(ParameterSet, count) == 0x18);
struct UpdatedObject;
struct UpdatedObjectVtable {
    void *unknown_slots[4];
    uint32_t (*start)(UpdatedObject *);
    void (*update)(UpdatedObject *, void *step);
    void (*finish)(UpdatedObject *);
    void *unknown_slots_1[2];
    void (*apply_parameters)(UpdatedObject *, ParameterSet *);
};
static_assert(offsetof(UpdatedObjectVtable, start) == 0x20 && offsetof(UpdatedObjectVtable, update) == 0x28);
static_assert(offsetof(UpdatedObjectVtable, finish) == 0x30 && offsetof(UpdatedObjectVtable, apply_parameters) == 0x48);

struct View {
    uint8_t unknown_0x00[0x8];
    MaskPair masks;
};
static_assert(offsetof(View, masks) == 0x8);

struct Owner;
struct OwnerVtable {
    void *unknown_slots[8];
    uint8_t (*visible_first)(Owner *);
    uint8_t (*visible_second)(Owner *);
};
static_assert(offsetof(OwnerVtable, visible_first) == 0x40 && offsetof(OwnerVtable, visible_second) == 0x48);
struct Owner {
    const OwnerVtable *vtable;
    uint8_t unknown_0x08[0x68];
    View *view;
};
static_assert(offsetof(Owner, view) == 0x70);

struct UpdatedObject {
    const UpdatedObjectVtable *vtable;
    int32_t references;
    uint32_t state;
    uint8_t flags;
    uint8_t unknown_0x11[0x7];
    Owner *owner;
    uint8_t unknown_0x20[0x10];
    MaskPair masks;
};
static_assert(offsetof(UpdatedObject, state) == 0xc && offsetof(UpdatedObject, flags) == 0x10);
static_assert(offsetof(UpdatedObject, owner) == 0x18 && offsetof(UpdatedObject, masks) == 0x30);

enum : uint32_t { state_idle = 0, state_running = 1, state_finishing = 2, state_done = 3 };

constexpr uint8_t flag_notify = 0x01;          // running: hand the object a parameter set
constexpr uint8_t flag_finish = 0x02;          // finish at the next chance
constexpr unsigned visible_shift = 2;          // the visible flag, 0x04
constexpr uint8_t flag_visible = 1 << visible_shift;
constexpr uint8_t flag_unknown_0x08 = 0x08;    // cleared with the changed flag
constexpr uint8_t flag_changed = 0x10;         // the visible flag changed
constexpr uint8_t flag_owner_decides = 0x20;   // the owner's query gives the visible flag
constexpr uint8_t flag_first_query = 0x40;     // ... its first query rather than its second

struct ViewSource {
    uint8_t unknown_0x00[0x4e0];
    MaskPair masks;
};
static_assert(offsetof(ViewSource, masks) == 0x4e0);
struct RendMan {
    uint8_t unknown_0x00[0x10];
    ViewSource *view_source;
};
static_assert(offsetof(RendMan, view_source) == 0x10);

RT_GLOBAL(0x05940298, rend_man_instance, RendMan *);
RT_GLOBAL(0x04d3b1e1, rend_man_name, const char);
RT_GLOBAL(0x0593e950, empty_masks, const MaskPair);
RT_GLOBAL(0x0593e970, default_view_masks, const MaskPair);

// The notify request's parameter set: one key, created on the shared allocator.
RT_GLOBAL(0x05940420, allocator_instance, engine::Allocator *);
RT_GLOBAL(0x04b35ce0, notify_parameter_keys, const uint8_t);
RT_ORIGINAL(0x02c51c30, create_parameter_set, ParameterSet *(const uint8_t *keys, uint32_t count, engine::Allocator *));
RT_ORIGINAL(0x02c51f60, set_parameter, void(ParameterSet *, uint32_t key, uint32_t value));
RT_ORIGINAL(0x02c51fc0, clear_parameter_set, void(ParameterSet *));
RT_ORIGINAL(0x0247b720, heap_owning, engine::Allocator *(const void *));
constexpr uint32_t notify_parameter_count = 1;
constexpr uint32_t notify_parameter_key = 0;
constexpr uint32_t float_one_bits = 0x3f800000;  // 1.0f, stored as its bit pattern

bool masks_clear(const uint32_t *masks, const uint32_t *selection, const uint32_t *empty)
{
    for (int lane = 0; lane < mask_lanes; ++lane)
        if ((masks[lane] & selection[lane]) != empty[lane])
            return false;
    return true;
}

uint8_t visible_in_view(const UpdatedObject *object, const Owner *owner)
{
    const MaskPair *view;
    if (owner->view) {
        view = &owner->view->masks;
    } else {
        RendMan *rend_man = engine::require(rend_man_instance, rend_man_name.address());
        view = rend_man->view_source ? &rend_man->view_source->masks : default_view_masks.address();
    }
    const MaskPair *empty = empty_masks.address();
    return !(masks_clear(view->first, object->masks.first, empty->first) &&
             masks_clear(view->second, object->masks.second, empty->second));
}

void refresh_visible(UpdatedObject *object)
{
    Owner *owner = object->owner;
    uint8_t visible;
    if (object->flags & flag_owner_decides)
        visible = object->flags & flag_first_query ? owner->vtable->visible_first(owner)
                                                   : owner->vtable->visible_second(owner);
    else
        visible = visible_in_view(object, owner);
    const uint8_t flags = object->flags;
    // The query's byte is compared and shifted in whole, as the game does.
    if (((flags >> visible_shift) & 1) != visible)
        object->flags = (flags & ~(flag_visible | flag_changed)) | uint8_t(visible << visible_shift) | flag_changed;
}

void notify(UpdatedObject *object)
{
    ParameterSet *parameters = create_parameter_set(notify_parameter_keys.address(), notify_parameter_count,
                                                    allocator_instance.get());
    if (!parameters)
        return;
    set_parameter(parameters, notify_parameter_key, float_one_bits);
    object->vtable->apply_parameters(object, parameters);
    engine::Allocator *heap = heap_owning(parameters);
    clear_parameter_set(parameters);
    heap->free(parameters);
}

// ---- The level copy ---------------------------------------------------------------------------

struct CopyBox {
    uint32_t left, top, front;
    uint32_t right, bottom, back;
};
static_assert(sizeof(CopyBox) == 0x18);

constexpr uint32_t level_missing_status = 0x80d30000;
constexpr uint32_t format_mask = 0xff;
constexpr uint32_t first_block_format = 0x23;     // the seven block-compressed formats
constexpr uint32_t block_format_count = 7;
constexpr uint32_t block_edge = 4;                // texels per block side

RT_ORIGINAL(0x014a31a0, copy_box,
            uint32_t(uintptr_t destination, uintptr_t source, const render::RecordGeometryDescriptor *,
                     const CopyBox *, uint32_t row_units, uint32_t slice_units));

bool block_compressed(uint32_t format)
{
    return (format & format_mask) - first_block_format < block_format_count;
}

uint32_t to_blocks(uint32_t texels)
{
    return (texels + block_edge - 1) / block_edge;
}

}  // namespace

extern "C" void bb_render_list_prepare_02379d60(List *self, ListNode *where, List *other, ListNode *first,
                                                ListNode *last, uint64_t count)
{
    ListAllocator *other_allocator = other->allocator;
    const uint32_t own_heap = self->allocator->vtable->heap_identity(self->allocator);
    if (own_heap == other_allocator->vtable->heap_identity(other_allocator)) {
        if (self != other) {
            if (list_max_size - self->size < count)
                list_too_long();
            self->size += count;
            other->size -= count;
        }
        relink(where, first, last);
        return;
    }
    for (ListNode *node = first; node != last; node = node->next)
        insert_value(self, where, &node->value);
    erase(other, first, last);
}

extern "C" uint8_t bb_render_object_update_02373850(UpdatedObject *object, void *step)
{
    if (object->owner)
        refresh_visible(object);

    uint32_t state = object->state;
    if (state == state_running) {
        if (object->flags & flag_finish) {
            object->state = state_finishing;
        } else {
            object->vtable->update(object, step);
            if (object->flags & flag_finish)
                object->state = state_finishing;
            else if (object->flags & flag_notify)
                notify(object);
        }
        object->flags &= ~(flag_changed | flag_unknown_0x08);
        state = object->state;
    } else if (state == state_idle) {
        if (object->flags & (flag_notify | flag_finish)) {
            object->state = state_finishing;
            state = state_finishing;
        } else {
            const uint32_t started = object->vtable->start(object);
            if (object->flags & flag_finish) {
                object->state = state_finishing;
                state = state_finishing;
            } else if (started) {
                object->state = state_running;
                return true;
            } else {
                state = object->state;
            }
        }
    }
    if (state == state_done)
        return false;
    if (state != state_finishing)
        return true;
    object->vtable->finish(object);
    object->state = state_done;
    return false;
}

extern "C" uint32_t bb_render_copy_initial_level_014a3410(uintptr_t destination, uintptr_t source,
                                                          const render::RecordGeometryDescriptor *level)
{
    if (!level)
        return level_missing_status;
    uint32_t width = level->first_08.get();
    uint32_t height = level->second_0c.get();
    CopyBox box{0, 0, 0, width, height, level->third_10.get()};
    const uint32_t format = level->encoding_04.get();
    if ((format != 0 && block_compressed(format)) || level->range_2c != 0) {
        box.left = to_blocks(box.left);
        box.top = to_blocks(box.top);
        box.right = to_blocks(box.right);
        box.bottom = to_blocks(box.bottom);
        width = to_blocks(width);
        height = to_blocks(height);
    }
    return copy_box(destination, source, level, &box, width, height * width);
}

extern "C" void bb_render_set_parameter_02c51f60(ParameterSet *set, uint32_t key, uint32_t value)
{
    const uint32_t *keys = set->keys;
    const uint32_t *end = keys + set->count;
    const uint32_t *found = std::lower_bound(keys, end, key);
    if (found != end && *found == key)
        set->values[found - keys] = value;
}

extern "C" void bb_render_clear_parameter_set_02c51fc0(ParameterSet *set)
{
    if (uint32_t *block = set->keys) {
        set->allocator->free(block);
        set->keys = nullptr;
    }
}
