// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: what a listener does with what its list hands it before the listeners are called
// (frame_timing_listener_list_prepare_013dab20, task_part_items.cpp).
//
// Neither keeps what it is given; it only says that the list has an owner or a named resource.
// - The owner: a listener that has no owner binding yet asks its own slot 6 for one.
// - The resource: while the named resource registry exists, a listener whose label is still empty
//   asks its slot 7 for a label source and takes the source's text (slot 5) as its label.
#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"
#include "../menu/scaleform_glue.h"
#include "runtime/original.h"

namespace {

struct LabelSource;
constexpr size_t label_source_text_slot = 5;
struct LabelSourceVtable {
    void *unknown_slots[label_source_text_slot];
    const char16_t *(*text)(LabelSource *);
};
static_assert(offsetof(LabelSourceVtable, text) == 0x28);
struct LabelSource {
    const LabelSourceVtable *vtable;
};

struct Listener;
constexpr size_t listener_bind_owner_slot = 6;
struct ListenerVtable {
    void *unknown_slots[listener_bind_owner_slot];
    void *(*bind_owner)(Listener *);
    LabelSource *(*label_source)(Listener *);
};
static_assert(offsetof(ListenerVtable, bind_owner) == 0x30 && offsetof(ListenerVtable, label_source) == 0x38);

struct Listener {
    const ListenerVtable *vtable;
    uint8_t unknown_0x08[0x48];
    void *owner_binding;   // what bind_owner gave; null until then
    engine::WideString label;
};
static_assert(offsetof(Listener, owner_binding) == 0x50 && offsetof(Listener, label) == 0x58);

struct ListenerOwner;   // only tested for null

RT_ORIGINAL(0x02a2a720, wide_string_assign_counted, engine::WideString *(engine::WideString *, const char16_t *text,
                                                                           uint64_t length));
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));

}  // namespace

// With an owner, binds a listener that has no owner binding yet.
extern "C" void bb_frame_timing_listener_take_owner_013d6880(Listener *listener, ListenerOwner *owner)
{
    if (owner && !listener->owner_binding)
        listener->owner_binding = listener->vtable->bind_owner(listener);
}

// With a resource, gives a listener whose label is empty its label source's text (no label
// source: nothing). An empty text is assigned with length 0 without measuring it.
extern "C" void bb_frame_timing_listener_take_resource_013d68b0(Listener *listener, void *resource)
{
    if (!menu::scaleform::named_resource_registry.get() || !resource || listener->label.size != 0)
        return;
    LabelSource *source = listener->vtable->label_source(listener);
    if (!source)
        return;
    const char16_t *text = source->vtable->text(source);
    const uint64_t length = text[0] != 0 ? c_wcslen(text) : 0;
    wide_string_assign_counted(&listener->label, text, length);
}
