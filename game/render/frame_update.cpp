// SPDX-License-Identifier: GPL-2.0-or-later
// Render: a per-frame update that ages a list of reference-counted objects and steps two children.
//
// render_frame_update_02377bd0 (0x02377bd0, once a frame in gameplay; `step` is the caller's
// frame argument, passed on unchanged):
//   1. when it has something to prepare, a prepare call that may change the list;
//   2. walks the list: each object is updated with the step; when the update returns false, the
//      reference is dropped (an atomic decrement: the last one destroys the object, a count
//      already at 0 or below reports "Invalid Unref()" through the engine's fatal error), and
//      the node is unlinked and freed;
//   3. when it has the +0x80 child, updates it with the render manager's +0x28 object;
//   4. when it has the +0x70 child, passes it a byte, then steps it with a frame time of 1/30 s
//      (0 when the frame is held), and clears the hold.
// BB_TARGET_FPS=uncapped does what "Uncap FPS++" does: step 4 passes the flipper's measured frame
// time whatever the hold says, and step 3 drops the render manager check.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../frame_timing/flipper.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

// A reference-counted object: the last reference destroys it through its first vtable slot.
struct RefObject;
struct RefObjectVtable {
    void (*destroy)(RefObject *);
};
struct RefObject {
    const RefObjectVtable *vtable;
    int32_t references;
};
static_assert(offsetof(RefObject, references) == 0x8);

struct ListNode {
    ListNode *next;
    ListNode *prev;
    RefObject *object;
};
static_assert(offsetof(ListNode, object) == 0x10);

// The engine's list: a sentinel node, a size, and the allocator its nodes come from.
struct RefList {
    void *proxy;
    ListNode *sentinel;
    uint64_t size;
    engine::Allocator *allocator;
};

struct Child70;
struct Child70Vtable {
    void *unknown_slots[8];
    void (*step)(Child70 *, float frame_time);
};
static_assert(offsetof(Child70Vtable, step) == 0x40);
struct Child70 {
    const Child70Vtable *vtable;
    uint8_t unknown_0x08[0x18];
    uint8_t value_0x20;   // given the updater's byte at +0x98 every frame
};
static_assert(offsetof(Child70, value_0x20) == 0x20);

struct Child80;
struct PrepareState;
struct RenderUpdater {
    uint8_t unknown_0x00[0x8];
    RefList list;
    PrepareState *prepare_state;      // its address is passed to the prepare call
    void **prepare_source;            // passed, and the pointer it holds
    void *prepare_pending;            // set: run the prepare call
    uint8_t unknown_0x40[0x30];
    Child70 *child_70;
    uint8_t unknown_0x78[0x8];
    Child80 *child_80;
    uint8_t unknown_0x88[0x4];
    uint32_t hold_frame;              // set: step the +0x70 child by 0
    uint8_t unknown_0x90[0x8];
    uint8_t value_0x98;
};
static_assert(offsetof(RenderUpdater, list) == 0x8 && offsetof(RenderUpdater, prepare_state) == 0x28);
static_assert(offsetof(RenderUpdater, prepare_source) == 0x30 && offsetof(RenderUpdater, prepare_pending) == 0x38);
static_assert(offsetof(RenderUpdater, child_70) == 0x70 && offsetof(RenderUpdater, child_80) == 0x80);
static_assert(offsetof(RenderUpdater, hold_frame) == 0x8c && offsetof(RenderUpdater, value_0x98) == 0x98);

struct RendMan {
    uint8_t unknown_0x00[0x28];
    void *object_0x28;
};
static_assert(offsetof(RendMan, object_0x28) == 0x28);

RT_ORIGINAL(0x02379d60, list_prepare,
            void(RefList *, ListNode *sentinel, PrepareState **, void *source_value, void **source, void *pending));
RT_ORIGINAL(0x02373850, object_update, uint8_t(RefObject *, void *step));
RT_ORIGINAL(0x0236e000, child_80_update, void(Child80 *, void *step, void *rendman_object));
RT_GLOBAL(0x05940298, rend_man_instance, RendMan *);
RT_GLOBAL(0x04d3b1e1, rend_man_name, const char);
RT_GLOBAL(0x04d40331, message_invalid_unref, const char);   // "Invalid Unref()"
RT_GLOBAL(0x04d29170, thirtieth_second, const float);
constexpr int32_t line_invalid_unref = 0x3e;

void release(RefObject *object)
{
    const int32_t before = __atomic_fetch_sub(&object->references, 1, __ATOMIC_SEQ_CST);
    if (before == 1)
        object->vtable->destroy(object);
    else if (before <= 0)
        engine::fatal_error(nullptr, line_invalid_unref, message_invalid_unref.address());
}

// Updates every object; drops and unlinks those whose update says they are done (and nodes
// that hold no object).
void age_objects(RenderUpdater *self, void *step)
{
    ListNode *node = self->list.sentinel->next;
    while (node != self->list.sentinel) {
        bool remove = true;
        if (node->object) {
            const uint8_t alive = object_update(node->object, step);
            RefObject *object = node->object;
            if (alive) {
                remove = object == nullptr;
            } else {
                release(object);
                node->object = nullptr;
            }
        }
        ListNode *next = node->next;
        if (remove && node != self->list.sentinel) {
            node->prev->next = next;
            node->next->prev = node->prev;
            self->list.allocator->free(node);
            self->list.size--;
        }
        node = next;
    }
}

}  // namespace

extern "C" void bb_render_frame_update_02377bd0(RenderUpdater *self, void *step)
{
    const bool uncapped = frame_timing::target_fps() == frame_timing::Target::uncapped;

    if (void *pending = self->prepare_pending)
        list_prepare(&self->list, self->list.sentinel, &self->prepare_state, *self->prepare_source,
                     self->prepare_source, pending);

    age_objects(self, step);

    if (Child80 *child = self->child_80) {
        RendMan *rend_man = rend_man_instance.get();
        if (!rend_man && !uncapped) engine::report_missing(rend_man_name.address());
        child_80_update(child, step, rend_man->object_0x28);
    }

    if (Child70 *child = self->child_70) {
        child->value_0x20 = self->value_0x98;
        float frame_time;
        if (uncapped)
            frame_time = frame_timing::flipper_instance.get()->frame_seconds;
        else
            frame_time = self->hold_frame != 0 ? 0.0f : thirtieth_second.get();
        child->vtable->step(child, frame_time);
        self->hold_frame = 0;
    }
}
