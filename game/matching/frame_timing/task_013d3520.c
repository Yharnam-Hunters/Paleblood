// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for a frame task that updates its three optional parts
// inside a timing scope.

#include <stdint.h>

enum {
    TIMING_SCOPE_ZERO = 0,
};

typedef struct {
    void *vtable;
    float seconds;
} FrameTime;

typedef struct {
    uint8_t unknown_0x00[0x10];
    void *part_0x10;
    void *part_0x18;
    uint8_t unknown_0x20[0x50];
    void *part_0x70;
} Task;

void task_scope_begin(int scope);
void task_scope_end(int scope);
void task_scope_finish(void);
void task_part_0x70_update(void *part);
void task_part_0x18_update(void *part, FrameTime *frame_time);
void task_part_0x10_update(void *part, float seconds);

// Updates each present part with this frame's time.
void frame_timing_task_013d3520(Task *task, FrameTime *frame_time)
{
    task_scope_begin(TIMING_SCOPE_ZERO);
    if (task->part_0x70)
        task_part_0x70_update(task->part_0x70);
    if (task->part_0x18)
        task_part_0x18_update(task->part_0x18, frame_time);
    if (task->part_0x10)
        task_part_0x10_update(task->part_0x10, frame_time->seconds);
    task_scope_end(TIMING_SCOPE_ZERO);
    task_scope_finish();
}
