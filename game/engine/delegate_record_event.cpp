// SPDX-License-Identifier: GPL-2.0-or-later
// The family posts_record_event_to_delegate_at_0x8 (symbols/families.csv): copies of one engine
// function. When the record it gets and the record's two pointers (+0 and +0x10) are set, and the
// delegate it holds at +8 has a bound object, it builds an event object on its stack (the base
// constructor, then the class's vtable and the two pointers), calls the delegate with it and its
// context (delegate_event.h), then destroys the event. The delegate's function is not tested.
#include <cstddef>
#include <cstdint>

#include "delegate_event.h"
#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x01400810, construct_event_base, void(void *event));
RT_ORIGINAL(0x013a75d0, destroy_event, void(void *event));

constexpr uint32_t record_event_vtable = 0x056f3170;
constexpr std::size_t vtable_header = 0x10;   // offset to top and type info before the entries

struct Record {
    void *first;
    uint8_t unknown_0x8[0x8];
    void *second;
};
static_assert(offsetof(Record, second) == 0x10);

struct RecordEvent {
    const void *vtable;
    uint8_t unknown_0x8[0x18];   // set by the base constructor
    void *second;
    void *first;
};
static_assert(offsetof(RecordEvent, second) == 0x20);
static_assert(sizeof(RecordEvent) == 0x30);

struct PosterAt0x8 {
    uint8_t unknown_0x0[0x8];
    engine::delegate_event::Delegate delegate;
};
static_assert(offsetof(PosterAt0x8, delegate.context) == 0x20);

}  // namespace

extern "C" void bb_engine_posts_record_event_to_delegate_at_0x8(void *poster, const Record *record)
{
    if (!record || !record->second || !record->first)
        return;
    const engine::delegate_event::Delegate &delegate = static_cast<const PosterAt0x8 *>(poster)->delegate;
    if (!delegate.object)
        return;
    RecordEvent event;
    construct_event_base(&event);
    event.vtable = static_cast<const uint8_t *>(rt_guest(record_event_vtable)) + vtable_header;
    event.second = record->second;
    event.first = record->first;
    engine::delegate_event::invoke(delegate, &event);
    destroy_event(&event);
}
