// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: helpers of the game's object and map instance updates (the game's ObjInsUpdateHelper
// and MapInsUpdateHelper, going by the source file names their reports carry).
//
// An instance that needs an update marks it in its update state and queues its id, once: a queue
// flag says it is already queued. A queue that is full is reported and written past all the same,
// as the shipped game does.
#include <cstddef>
#include <cstdint>

#include "engine.h"

namespace {

// A queue of instance ids waiting for an update.
struct UpdateQueue {
    uint32_t capacity;
    uint8_t unknown_0x04[0xc];
    uint32_t count;
    uint8_t unknown_0x14[0x4];
    uint16_t *ids;
};
static_assert(offsetof(UpdateQueue, count) == 0x10 && offsetof(UpdateQueue, ids) == 0x18);

// An object instance's update state.
struct ObjectUpdateState {
    UpdateQueue *queue;
    uint16_t id;
    uint16_t requests;     // what the update is asked to do
    uint16_t queue_flags;  // object_queued: the id is in the queue
};
static_assert(offsetof(ObjectUpdateState, id) == 0x8 && offsetof(ObjectUpdateState, queue_flags) == 0xc);

// A map instance's update state: the same, with a byte of queue flags.
struct MapUpdateState {
    UpdateQueue *queue;
    uint16_t id;
    uint16_t requests;
    uint8_t queue_flags;  // map_queued: the id is in the queue
};
static_assert(offsetof(MapUpdateState, id) == 0x8 && offsetof(MapUpdateState, queue_flags) == 0xc);

constexpr uint16_t object_update_request = 0x20;
constexpr uint16_t object_queued = 0x1000;
constexpr uint16_t map_update_request = 0x4;
constexpr uint8_t map_queued = 0x1;

struct ObjectInstance {
    uint8_t unknown_0x000[0x10];
    ObjectUpdateState *update;
    uint8_t unknown_0x018[0x210 - 0x18];
    uint8_t part_0x210[1];  // a part the update request steps first (layout unknown)
};
static_assert(offsetof(ObjectInstance, update) == 0x10 && offsetof(ObjectInstance, part_0x210) == 0x210);

struct MapInstance {
    uint8_t unknown_0x000[0xc8];
    MapUpdateState *update;
};
static_assert(offsetof(MapInstance, update) == 0xc8);

RT_ORIGINAL(0x01beca10, step_object_part, void(uint8_t *part));

RT_GLOBAL(0x04d349b4, object_helper_source_file, const char);
RT_GLOBAL(0x04d331f5, map_helper_source_file, const char);
RT_GLOBAL(0x04b388a8, empty_message, const char);
constexpr int32_t object_full_queue_line = 0x106;
constexpr int32_t map_full_queue_line = 0xcc;

void push_id(UpdateQueue *queue, uint16_t id)
{
    queue->ids[queue->count++] = id;
}

// A registration of an item in its owner's list (a Dantelion array of pointers).
struct ItemList {
    void **items;
    int32_t count;
    uint32_t capacity_and_flags;  // the low 30 bits are the capacity
};
static_assert(sizeof(ItemList) == 0x10);
constexpr uint32_t capacity_mask = 0x3fffffff;

struct ListOwner {
    uint8_t unknown_0x00[0xa8];
    ItemList list;
};
static_assert(offsetof(ListOwner, list) == 0xa8);

struct Registration {
    uint8_t unknown_0x00[0x8];
    ListOwner *owner;
    void *current;  // the item this registration put in the owner's list
};
static_assert(offsetof(Registration, owner) == 0x8 && offsetof(Registration, current) == 0x10);

// The array's growth (to twice its count, or 1) on the allocator the owner's lists use.
struct Allocator;
RT_GLOBAL(0x058018b0, list_allocator, Allocator);
RT_ORIGINAL(0x00819100, grow_list, void(Allocator *allocator, ItemList *list, int32_t element_size));

}  // namespace

// Asks for an object instance's update: steps the part at +0x210, sets the update request and
// queues the instance's id unless it is already queued.
extern "C" void bb_engine_obj_ins_request_update_019a7090(ObjectInstance *instance)
{
    step_object_part(instance->part_0x210);
    ObjectUpdateState *update = instance->update;
    update->requests |= object_update_request;
    if (update->queue_flags & object_queued)
        return;
    UpdateQueue *queue = update->queue;
    if (queue->count >= queue->capacity)
        engine::fatal_error(object_helper_source_file.address(), object_full_queue_line, empty_message.address());
    update->queue_flags |= object_queued;
    push_id(queue, update->id);
}

// Asks for a map instance's update: sets the update request and queues the instance's id unless
// it is already queued.
extern "C" void bb_engine_map_ins_request_update_0198bf70(MapInstance *instance)
{
    MapUpdateState *update = instance->update;
    update->requests |= map_update_request;
    if (update->queue_flags & map_queued)
        return;
    UpdateQueue *queue = update->queue;
    if (queue->count >= queue->capacity)
        engine::fatal_error(map_helper_source_file.address(), map_full_queue_line, empty_message.address());
    update->queue_flags |= map_queued;
    push_id(queue, update->id);
}

// Moves a registration to `item`: the item it registered before is taken out of the owner's list
// (the last item moves into its place), and `item` is appended, growing the list when it is
// full. Nothing happens without an owner or an item.
//
// An earlier item missing from the list still counts the list down by one, and the last item is
// then copied to the slot before the list's first (index -1), as the shipped game does.
extern "C" void bb_engine_obj_ins_set_registration_01c44210(Registration *registration, void *item)
{
    ListOwner *owner = registration->owner;
    if (!owner || !item)
        return;
    ItemList &list = owner->list;
    if (registration->current) {
        int32_t index = -1;
        for (int32_t i = 0; i < list.count; ++i) {
            if (list.items[i] == registration->current) {
                index = i;
                break;
            }
        }
        --list.count;
        list.items[index] = list.items[list.count];  // the last item onto itself when it was the one taken out
    }
    if (static_cast<uint32_t>(list.count) == (list.capacity_and_flags & capacity_mask))
        grow_list(list_allocator.address(), &list, sizeof(void *));
    list.items[list.count++] = item;
    registration->current = item;
}
