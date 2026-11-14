// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the task manager's worker (the four calls the worker step and the dispatcher make
// in task_manager.cpp) and the task call the dispatcher makes for each registered task.
//
// A worker holds slots, each a lane and the batch assigned to it. A lane in mode 0 runs its batch
// on the calling thread; any other mode hands it to a lane thread.
// - frame_timing_task_worker_assign_0143e030 (worker_run in task_manager.cpp): assigns a batch to
//   the slot at an index; false while the worker is busy, for a null batch, a negative index or one
//   past the end (an unsigned slot count).
// - frame_timing_task_worker_start_0143de20 (worker_step_a): unless busy, marks the worker busy and
//   starts every slot that has a batch: a caller-thread lane is only marked running, a thread lane
//   is started (and signalled) by its own start.
// - frame_timing_task_worker_run_0143deb0 (worker_step_b): when busy, runs every caller-thread lane
//   that has a batch.
// - frame_timing_task_worker_finish_0143df10 (worker_step_c): when busy, waits until no lane is
//   running: a running caller-thread lane is run here and the scan starts again from the first
//   slot; a running thread lane is scanned again until its thread stops it (this spins, as the
//   original does). Then takes every batch back (finishing caller-thread lanes) and clears busy.
// - frame_timing_task_clear_items_0143ec40 (task_wake in task_manager.cpp): under the task's lock,
//   frees every pending item (the last first) and then their array through the task's allocator,
//   and empties the list; a second list is emptied (not freed) when its count is not zero.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "runtime/original.h"

namespace {

// A lane: runs batches on the calling thread (mode 0) or on its own thread.
struct WorkerLane {
    uint8_t unknown_0x00[0x48];
    int32_t mode;
    uint8_t unknown_0x4c[0x2];
    uint8_t running;
};
static_assert(offsetof(WorkerLane, mode) == 0x48 && offsetof(WorkerLane, running) == 0x4e);
constexpr int32_t mode_caller_thread = 0;

struct TaskBatch;
struct WorkerSlot {
    WorkerLane *lane;
    TaskBatch *batch;   // null: nothing assigned
};
static_assert(sizeof(WorkerSlot) == 0x10 && offsetof(WorkerSlot, batch) == 0x8);

struct TaskWorker {
    uint8_t unknown_0x00[0x18];
    WorkerSlot *slots_begin;   // std::vector<WorkerSlot>
    WorkerSlot *slots_end;
    uint8_t unknown_0x28[0x10];
    uint8_t busy;   // set by start, cleared by finish
};
static_assert(offsetof(TaskWorker, slots_begin) == 0x18 && offsetof(TaskWorker, slots_end) == 0x20);
static_assert(offsetof(TaskWorker, busy) == 0x38);

// The slot count as the original computes it: a signed byte difference shifted down, then used
// as an unsigned count.
size_t slot_count(const TaskWorker *worker)
{
    return static_cast<size_t>(worker->slots_end - worker->slots_begin);
}

// The lane's own steps (the original's).
RT_ORIGINAL(0x01467500, lane_start_on_caller, void(WorkerLane *, TaskBatch *));
RT_ORIGINAL(0x01467300, lane_start_on_thread, void(WorkerLane *, TaskBatch *));
RT_ORIGINAL(0x01467530, lane_run, void(WorkerLane *));
RT_ORIGINAL(0x014675a0, lane_finish, void(WorkerLane *));

// A task's lock (an object inside the task) and its pending items.
struct TaskLock;
struct TaskLockVtable {
    void *unknown_slots[3];
    void (*lock)(TaskLock *, uint32_t timeout);
    void *unknown_slots_2[1];
    void (*unlock)(TaskLock *);
};
static_assert(offsetof(TaskLockVtable, lock) == 0x18 && offsetof(TaskLockVtable, unlock) == 0x28);
struct TaskLock {
    const TaskLockVtable *vtable;
};
constexpr uint32_t wait_forever = 0xffffffff;

struct Task {
    uint8_t unknown_0x00[0x60];
    void **items;   // pending items, each freed through the allocator
    uint64_t item_count;
    void *second_items;   // emptied, not freed
    uint64_t second_count;
    engine::Allocator *allocator;
    uint8_t unknown_0x88[0x8];
    TaskLock lock;
};
static_assert(offsetof(Task, items) == 0x60 && offsetof(Task, item_count) == 0x68);
static_assert(offsetof(Task, second_items) == 0x70 && offsetof(Task, second_count) == 0x78);
static_assert(offsetof(Task, allocator) == 0x80 && offsetof(Task, lock) == 0x90);

}  // namespace

extern "C" bool bb_frame_timing_task_worker_assign_0143e030(TaskWorker *worker, int32_t index, TaskBatch *batch)
{
    if (worker->busy || !batch || index < 0) return false;
    if (static_cast<size_t>(index) >= slot_count(worker)) return false;
    worker->slots_begin[index].batch = batch;
    return true;
}

extern "C" void bb_frame_timing_task_worker_start_0143de20(TaskWorker *worker)
{
    if (worker->busy) return;
    worker->busy = 1;
    const size_t count = slot_count(worker);
    for (size_t i = 0; i != count; i++) {
        const WorkerSlot &slot = worker->slots_begin[i];   // the vector read again each time
        if (!slot.batch) continue;
        if (slot.lane->mode == mode_caller_thread)
            lane_start_on_caller(slot.lane, slot.batch);
        else
            lane_start_on_thread(slot.lane, slot.batch);
    }
}

extern "C" void bb_frame_timing_task_worker_run_0143deb0(TaskWorker *worker)
{
    if (!worker->busy) return;
    const size_t count = slot_count(worker);
    for (size_t i = 0; i != count; i++) {
        const WorkerSlot &slot = worker->slots_begin[i];
        if (slot.batch && slot.lane->mode == mode_caller_thread) lane_run(slot.lane);
    }
}

extern "C" void bb_frame_timing_task_worker_finish_0143df10(TaskWorker *worker)
{
    if (!worker->busy) return;
    const size_t count = slot_count(worker);
    // Wait until no lane runs: a running caller-thread lane is run here, then the scan starts again.
    for (size_t i = 0; i < count;) {
        WorkerLane *lane = worker->slots_begin[i].lane;
        if (!lane->running) {
            i++;
            continue;
        }
        if (lane->mode == mode_caller_thread) lane_run(lane);
        i = 0;
    }
    // Take every batch back.
    for (size_t i = 0; i != count; i++) {
        WorkerSlot &slot = worker->slots_begin[i];
        if (!slot.batch) continue;
        slot.batch = nullptr;
        if (slot.lane->mode == mode_caller_thread) lane_finish(slot.lane);
    }
    worker->busy = 0;
}

extern "C" void bb_frame_timing_task_clear_items_0143ec40(Task *task)
{
    TaskLock *lock = &task->lock;
    lock->vtable->lock(lock, wait_forever);
    engine::Allocator *array_allocator = task->allocator;   // read once here for the array
    if (task->second_count) {
        task->second_count = 0;
        task->second_items = nullptr;
    }
    for (uint64_t left = task->item_count; left != 0;) {
        void *item = task->items[--left];   // the array and the allocator read again each time
        if (item) task->allocator->free(item);
    }
    if (task->items) array_allocator->free(task->items);
    task->item_count = 0;
    task->items = nullptr;
    lock->vtable->unlock(lock);
}
