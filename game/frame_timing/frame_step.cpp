// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the per-frame step of the main loop.
//
// frame_timing_frame_step (0x02418d20): asks the SprjWindow whether the game is running,
// creates the SprjFlipper on first use, runs the frame limiter, runs the task update with a
// 1/30 s frame time, and returns "running and no quit requested". Missing singletons are
// reported exactly where the original reports them, and it carries on as the original does.
//
// With BB_TARGET_FPS set, the task update's frame time is the one the community frame-rate
// patches compute here: 1/60 s (1/30 s for 30), unless the flipper's interval is not 1/30 s; then
// the measured frame time clamped to [interval, 1/30 s] (maxss then minss, so a NaN goes the same
// way). The patches also drop the SprjTask check; we keep it.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "flipper.h"
#include "frame_time.h"
#include "runtime/original.h"
#include "target_fps.h"

namespace {

using namespace frame_timing;

// Objects whose one virtual method the step calls
struct SprjWindow;
struct SprjWindowVtable {
    void *unknown_slots[3];
    uint8_t (*is_running)(SprjWindow *);
};
struct SprjWindow {
    const SprjWindowVtable *vtable;
};

struct QuitGate;
struct QuitGateVtable {
    void *unknown_slots[5];
    uint8_t (*needs_window)(QuitGate *);
};
struct QuitGate {
    const QuitGateVtable *vtable;
};

struct QuitQuery;
struct QuitQueryVtable {
    void *unknown_slots[19];
    uint8_t (*quit_requested)(QuitQuery *);
};
struct QuitQuery {
    const QuitQueryVtable *vtable;
};
static_assert(offsetof(SprjWindowVtable, is_running) == 0x18 && offsetof(QuitGateVtable, needs_window) == 0x28);
static_assert(offsetof(QuitQueryVtable, quit_requested) == 0x98);

struct SprjTask;

RT_GLOBAL(0x05940500, window_instance, SprjWindow *);
RT_GLOBAL(0x05940408, flipper_allocator_instance, engine::Allocator *);
RT_GLOBAL(0x05940510, task_instance, SprjTask *);
RT_GLOBAL(0x05a9fa30, quit_gate_instance, QuitGate *);
RT_GLOBAL(0x05aa54f8, quit_query_instance, QuitQuery *);
RT_GLOBAL(0x04d3b00c, window_name, const char);
RT_GLOBAL(0x04d3b288, task_name, const char);

RT_ORIGINAL(0x024e2550, quit_query_create, QuitQuery *());
RT_ORIGINAL(0x02434520, flipper_construct, void(SprjFlipper *));
RT_ORIGINAL(0x02434770, pace_frame, void(SprjFlipper *));
// Its first argument is not read (the original leaves the register as it is).
RT_ORIGINAL(0x024512a0, task_update, void(void *unused, FrameTime *));

constexpr uint64_t flipper_alignment = 8;

SprjFlipper *flipper_on_first_use()
{
    if (SprjFlipper *flipper = flipper_instance.get()) return flipper;
    auto *flipper = static_cast<SprjFlipper *>(
        flipper_allocator_instance.get()->allocate(sizeof(SprjFlipper), flipper_alignment));
    if (flipper) {
        flipper_construct(flipper);
        flipper_instance.get() = flipper;
    } else {
        flipper_instance.get() = nullptr;
        engine::report_missing(flipper_name.address());
    }
    flipper->no_late_windows = 1;
    return flipper;
}

// The task update's frame time: 1/30 s, or what the frame-rate patches compute (see the top).
float task_frame_time(const SprjFlipper *flipper)
{
    const float thirty = std::bit_cast<float>(k_interval_30);
    const Target target = target_fps();
    if (target == Target::original) return thirty;
    float seconds = target == Target::fps30 ? thirty : std::bit_cast<float>(k_interval_60);
    if (std::bit_cast<uint32_t>(flipper->interval) != k_interval_30)
        seconds = engine::min_of(engine::max_of(flipper->frame_seconds, flipper->interval), thirty);
    return seconds;
}

}  // namespace

extern "C" uint8_t bb_frame_timing_frame_step()
{
    SprjWindow *window = window_instance.get();
    if (!window) engine::report_missing(window_name.address());
    const uint8_t running = window->vtable->is_running(window);

    SprjFlipper *flipper = flipper_on_first_use();
    pace_frame(flipper);

    FrameTime frame_time{frame_time_vtable.address() + engine::vtable_address_point, task_frame_time(flipper)};
    if (!task_instance.get()) engine::report_missing(task_name.address());
    task_update(nullptr, &frame_time);

    bool needs_window = true;
    if (QuitGate *gate = quit_gate_instance.get()) needs_window = gate->vtable->needs_window(gate) != 0;
    if (needs_window && !window_instance.get()) engine::report_missing(window_name.address());

    QuitQuery *query = quit_query_instance.get();
    if (!query) query = quit_query_create();
    const uint8_t quitting = query->vtable->quit_requested(query);
    // bitwise, as the original: "running" and "not quitting" from the low bits
    return running & (quitting ^ 1);
}
