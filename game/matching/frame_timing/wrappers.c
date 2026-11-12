// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for small frame-timing helpers.

#define MICROSECONDS_PER_MILLISECOND 1000
#define ALL_TASKS (-1)
#define FRAME_VALUE_INDEX 2

int sceKernelUsleep(unsigned microseconds);
void frame_timing_task_run(void *manager, int task, void *frame);

typedef struct {
    unsigned long long unknown_0x00;
    float value;
} FrameValue;
extern FrameValue frame_timing_frame_value;

int frame_timing_sleep_ms(unsigned milliseconds)
{
    sceKernelUsleep(milliseconds * MICROSECONDS_PER_MILLISECOND);
    return 0;
}

// Runs every task of the manager for this frame (task index -1 means all).
void frame_timing_task_run_all_01388c60(void *manager, void *frame)
{
    frame_timing_task_run(manager, ALL_TASKS, frame);
}

// Stores the frame's third float into the global frame value.
void frame_timing_task_set_frame_value_0143f9f0(const float *frame)
{
    frame_timing_frame_value.value = frame[FRAME_VALUE_INDEX];
}
