// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the helpers the per-frame task (0x013d3520) calls around its parts.
//
// - profile_start / profile_stop (0x01456eb0 / 0x01456ed0): when profiling is on, start or stop
//   one of the ten profiling stopwatches (a tail call to the stopwatch's own start or stop).
// - profile_frame_end (0x01456ef0): when profiling is on, give each of the ten stopwatches its
//   end-of-frame tick, in table order (the last one a tail call).
// - item_list_update (0x01456c80): the task's part at +0x70 holds a list of item pointers; each
//   item is updated in turn. The list ends are read once, before the first item.
// - listener_list_notify (0x013daac0): the task's part at +0x18 first runs its own preparation,
//   then passes the frame-time descriptor to every listener in its list through the listener's
//   third virtual slot. The list is read again after every call, so a listener may grow or
//   shrink it.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"
#include "frame_time.h"

namespace {

using frame_timing::FrameTime;

struct Stopwatch;   // game/frame_timing/stopwatch.cpp; only passed on here
struct Item;        // opaque: only passed on

constexpr std::size_t profile_stopwatch_count = 10;

// Nonzero when the profiling stopwatches run.
RT_GLOBAL(0x058b8718, profiling_enabled, const uint8_t);
RT_GLOBAL(0x058b86c0, profile_stopwatches, Stopwatch *const[profile_stopwatch_count]);

RT_ORIGINAL(0x01448580, stopwatch_start, void(Stopwatch *));
RT_ORIGINAL(0x01448620, stopwatch_stop, void(Stopwatch *));
RT_ORIGINAL(0x014486d0, stopwatch_frame_end, void(Stopwatch *));

struct ItemList {
    uint8_t unknown_0x00[0x8];
    Item **begin;
    Item **end;
};
static_assert(offsetof(ItemList, begin) == 0x8 && offsetof(ItemList, end) == 0x10);

RT_ORIGINAL(0x013d4810, item_update, void(Item *));

struct Listener;
struct ListenerVtable {
    void *unknown_slot_0;
    void *unknown_slot_1;
    void (*on_frame)(Listener *, FrameTime *);
};
static_assert(offsetof(ListenerVtable, on_frame) == 0x10);

struct Listener {
    const ListenerVtable *vtable;
};

struct ListenerList {
    uint8_t unknown_0x00[0x20];
    Listener **begin;
    Listener **end;
};
static_assert(offsetof(ListenerList, begin) == 0x20 && offsetof(ListenerList, end) == 0x28);

RT_ORIGINAL(0x013dab20, listener_list_prepare, void(ListenerList *));

}  // namespace

// The index is taken as unsigned (zero-extended), so only 0..9 name a stopwatch.
extern "C" void bb_frame_timing_profile_start_01456eb0(uint32_t stopwatch)
{
    if (profiling_enabled.get() == 0) return;
    stopwatch_start(profile_stopwatches.get()[stopwatch]);
}

extern "C" void bb_frame_timing_profile_stop_01456ed0(uint32_t stopwatch)
{
    if (profiling_enabled.get() == 0) return;
    stopwatch_stop(profile_stopwatches.get()[stopwatch]);
}

// The switch is read once; each table entry is read just before its own call.
extern "C" void bb_frame_timing_profile_frame_end_01456ef0()
{
    if (profiling_enabled.get() == 0) return;
    for (std::size_t i = 0; i < profile_stopwatch_count; ++i)
        stopwatch_frame_end(profile_stopwatches.get()[i]);
}

extern "C" void bb_frame_timing_item_list_update_01456c80(ItemList *list)
{
    Item **const end = list->end;
    for (Item **item = list->begin; item != end; ++item)
        item_update(*item);
}

// The index is 32 bits wide and compared unsigned against the current list length.
extern "C" void bb_frame_timing_listener_list_notify_013daac0(ListenerList *list, FrameTime *frame_time)
{
    listener_list_prepare(list);
    for (uint32_t i = 0; i < static_cast<std::size_t>(list->end - list->begin); ++i) {
        Listener *listener = list->begin[i];
        listener->vtable->on_frame(listener, frame_time);
    }
}
