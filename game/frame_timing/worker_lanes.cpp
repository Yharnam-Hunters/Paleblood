// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the worker lane's own steps, which the task manager's worker calls
// (task_manager_workers.cpp). A lane runs the tasks of the batch it is given: a lane in mode 0 on
// the thread that calls it, any other mode on its own thread, which the start wakes up.
//
// - frame_timing_worker_lane_start_on_caller_01467500 (the worker's start, mode 0 lanes): takes a
//   batch for the calling thread. Nothing happens for a null batch or while the lane is running
//   or already taken by its caller; otherwise the lane is marked taken and running, its stop
//   request is cleared and the batch kept. No task runs yet.
// - frame_timing_worker_lane_start_on_thread_01467300 (the worker's start, other modes): the same
//   checks; then the lane is marked running (not taken), its stop request cleared and the batch
//   kept. A lane in any mode but 0 has its thread woken through its sync object. A lane in mode 0
//   runs the batch here at once: task after task until the batch is empty or a stop is
//   requested, each task's run given the same argument word (0 to begin with, and whatever a run
//   left in it); then the stop request and running are cleared.
// - frame_timing_worker_lane_run_01467530 (the worker's run and finish, mode 0 lanes): runs one
//   task of a lane its caller took and that is still running, with the lane's mode as the run's
//   argument; when the batch is empty the lane stops running instead.
// - frame_timing_worker_lane_finish_014675a0 (the worker's finish, mode 0 lanes): gives a taken
//   lane back: not taken, not running, no stop request and no batch. A lane not taken is left as
//   it is.
// - frame_timing_task_batch_take_next_0143d760 (the batch's take): a batch is a double-ended queue
//   of task pointers in blocks of two. An empty batch gives null. Otherwise the front task is read
//   through the queue's proxy (the container it names: its block map, whose size is a power of
//   two, indexed by the front's block), the size drops by one and the front moves on, back to 0
//   when the batch is left empty.
// - frame_timing_lane_sync_signal_02aa4080 (the sync object's signal): a sync object that its
//   second slot says was never created is reported (the engine's fatal error, which returns);
//   either way its event is signalled.
//
// task_manager_workers.cpp keeps its own partial WorkerLane, so that file and its verification
// rows stay as they are.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

// A task of a batch: its third virtual function runs it, given a pointer to an argument word.
struct Task;
struct TaskVtable {
    void *unknown_slots[2];
    void (*run)(Task *task, int32_t *argument);
};
static_assert(offsetof(TaskVtable, run) == 0x10);
struct Task {
    const TaskVtable *vtable;
};

// The tasks left to run: a double-ended queue of task pointers, two to a block. Its proxy names
// the container its iterators read the block map from.
struct TaskBatch;
struct TaskBatchProxy {
    TaskBatch *container;
};
struct TaskBatch {
    TaskBatchProxy *proxy;
    Task ***blocks;
    uint64_t block_count;   // a power of two
    uint64_t front;         // the front task's position
    uint64_t size;
};
static_assert(offsetof(TaskBatch, blocks) == 0x8 && offsetof(TaskBatch, block_count) == 0x10);
static_assert(offsetof(TaskBatch, front) == 0x18 && offsetof(TaskBatch, size) == 0x20);
constexpr uint64_t tasks_per_block = 2;

// The lane's sync object: what its thread waits on (Dantelion's event). Its second slot tells
// whether the event was created.
struct LaneSync;
struct LaneSyncVtable {
    void *unknown_slots[2];
    bool (*is_created)(LaneSync *);
};
static_assert(offsetof(LaneSyncVtable, is_created) == 0x10);
struct LaneEvent {
    uint8_t unknown_0x00[0x30];
};
struct LaneSync {
    const LaneSyncVtable *vtable;
    LaneEvent event;
};
static_assert(offsetof(LaneSync, event) == 0x8 && sizeof(LaneSync) == 0x38);

struct WorkerLane {
    uint8_t unknown_0x00[0x10];
    LaneSync sync;                  // signalled to wake the lane's thread
    int32_t mode;                   // 0: runs on the caller's thread
    uint8_t unknown_0x4c[0x2];
    uint8_t running;
    uint8_t stop_requested;         // ends a run of tasks on the calling thread
    uint8_t taken_by_caller;        // started on the caller's thread and not finished yet
    uint8_t unknown_0x51[0x7];
    TaskBatch *batch;
};
static_assert(offsetof(WorkerLane, sync) == 0x10 && offsetof(WorkerLane, mode) == 0x48);
static_assert(offsetof(WorkerLane, running) == 0x4e && offsetof(WorkerLane, stop_requested) == 0x4f);
static_assert(offsetof(WorkerLane, taken_by_caller) == 0x50 && offsetof(WorkerLane, batch) == 0x58);
static_assert(sizeof(WorkerLane) == 0x60);
constexpr int32_t mode_caller_thread = 0;

// The batch's next task, taken off its front (null when the batch is empty).
RT_ORIGINAL(0x0143d760, batch_take_next, Task *(TaskBatch *));
// Signals a sync object (a missing one is reported first).
RT_ORIGINAL(0x02aa4080, sync_signal, void(LaneSync *));
RT_ORIGINAL(0x02a3b050, event_signal, void(LaneEvent *));
// The report of a sync object that was never created: its source file, line and message.
RT_GLOBAL(0x04c6ea8c, sync_source_file, const char);
RT_GLOBAL(0x04c6eaf5, sync_not_created_message, const char);
constexpr int32_t sync_not_created_line = 0x83;

bool can_start(const WorkerLane *lane, const TaskBatch *batch)
{
    return batch && !lane->running && !lane->taken_by_caller;
}

}  // namespace

extern "C" void bb_frame_timing_worker_lane_start_on_caller_01467500(WorkerLane *lane, TaskBatch *batch)
{
    if (!can_start(lane, batch)) return;
    lane->taken_by_caller = 1;
    lane->running = 1;
    lane->stop_requested = 0;
    lane->batch = batch;
}

extern "C" void bb_frame_timing_worker_lane_start_on_thread_01467300(WorkerLane *lane, TaskBatch *batch)
{
    if (!can_start(lane, batch)) return;
    lane->running = 1;
    lane->stop_requested = 0;
    lane->batch = batch;
    if (lane->mode != mode_caller_thread) {
        sync_signal(&lane->sync);
        return;
    }
    // Mode 0: the whole batch runs here. The argument word is shared by every run.
    int32_t argument = 0;
    while (!lane->stop_requested) {
        Task *task = batch_take_next(lane->batch);   // the batch read again for every task
        if (!task) break;
        task->vtable->run(task, &argument);
    }
    lane->stop_requested = 0;
    lane->running = 0;
}

extern "C" void bb_frame_timing_worker_lane_run_01467530(WorkerLane *lane)
{
    if (!lane->taken_by_caller || !lane->running) return;
    Task *task = batch_take_next(lane->batch);
    if (!task) {
        lane->running = 0;
        return;
    }
    int32_t argument = lane->mode;   // read after the task is taken
    task->vtable->run(task, &argument);
}

extern "C" void bb_frame_timing_worker_lane_finish_014675a0(WorkerLane *lane)
{
    if (!lane->taken_by_caller) return;
    lane->taken_by_caller = 0;
    lane->running = 0;
    lane->stop_requested = 0;
    lane->batch = nullptr;
}

extern "C" Task *bb_frame_timing_task_batch_take_next_0143d760(TaskBatch *batch)
{
    uint64_t size = batch->size;
    if (size == 0)
        return nullptr;
    const TaskBatch *container = batch->proxy->container;
    const uint64_t front = batch->front;
    const uint64_t block = (front / tasks_per_block) & (container->block_count - 1);
    Task *task = container->blocks[block][front % tasks_per_block];
    --size;
    batch->size = size;
    batch->front = size == 0 ? 0 : front + 1;
    return task;
}

extern "C" void bb_frame_timing_lane_sync_signal_02aa4080(LaneSync *sync)
{
    if (!sync->vtable->is_created(sync))
        engine::fatal_error(sync_source_file.address(), sync_not_created_line, sync_not_created_message.address());
    event_signal(&sync->event);
}
