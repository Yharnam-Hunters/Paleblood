// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: a per-frame task that hands the frame time to its parts.
//
// frame_timing_task_013d3520 (0x013d3520, called once a frame in gameplay) takes the frame-time
// descriptor ({descriptor vtable, seconds}): a begin call, then its part at +0x70, the descriptor
// to its part at +0x18, the seconds to its part at +0x10, each only when present, then two end
// calls (the last one a tail call). What the parts are is not known yet.
//
// BB_TARGET_FPS=uncapped does what "Uncap FPS++" does here: the seconds are read, the caller's
// descriptor is set to 1.0, the seconds are divided by that 1.0, and the +0x10 part's call is
// made even when the part is missing.
#include <cstddef>
#include <cstdint>

#include "../engine/scalar.h"
#include "runtime/original.h"
#include "frame_time.h"
#include "target_fps.h"

namespace {

using frame_timing::FrameTime;

struct Part;   // opaque: only passed on

struct Task {
    uint8_t unknown_0x00[0x10];
    Part *part_0x10;
    Part *part_0x18;
    uint8_t unknown_0x20[0x50];
    Part *part_0x70;
};
static_assert(offsetof(Task, part_0x10) == 0x10 && offsetof(Task, part_0x18) == 0x18 && offsetof(Task, part_0x70) == 0x70);

RT_ORIGINAL(0x01456eb0, task_begin, void(int32_t));
RT_ORIGINAL(0x01456c80, part_0x70_update, void(Part *));
RT_ORIGINAL(0x013daac0, part_0x18_update, void(Part *, FrameTime *));
RT_ORIGINAL(0x013d5440, part_0x10_update, void(Part *, float seconds));
RT_ORIGINAL(0x01456ed0, task_end, void(int32_t));
RT_ORIGINAL(0x01456ef0, task_finish, void());

}  // namespace

extern "C" void bb_frame_timing_task_013d3520(Task *task, FrameTime *frame_time)
{
    task_begin(0);
    if (Part *part = task->part_0x70) part_0x70_update(part);
    if (Part *part = task->part_0x18) part_0x18_update(part, frame_time);
    Part *part_0x10 = task->part_0x10;
    if (frame_timing::target_fps() == frame_timing::Target::uncapped) {
        const float seconds = frame_time->seconds;
        frame_time->seconds = 1.0f;
        part_0x10_update(part_0x10, engine::divide(seconds, frame_time->seconds));
    } else if (part_0x10) {
        part_0x10_update(part_0x10, frame_time->seconds);
    }
    task_end(0);
    task_finish();
}
