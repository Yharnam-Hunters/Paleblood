// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the task manager's frame (FD4::FD4TaskManager).
//
// The frame step runs the frame's tasks through task_frame -> task_run_all -> task_run, which
// brackets the dispatch with setup and teardown:
// - frame_timing_task_frame_024512a0: the frame step's entry: the task manager singleton (a
//   missing one is reported and read again), then task_run_all. Its first argument is not used.
// - frame_timing_task_run_all_01388c60: runs the tasks of every group.
// - frame_timing_task_run_01388c70: unless the manager is already running, prepares the frame
//   (frame state, the "before" queue, the workers, the frame value), runs the dispatcher with
//   the running flag set, then finishes (the "after" queue, the workers, the frame state).
// - frame_timing_task_flush_queue_0143e490: under the queue's lock, runs every queued object,
//   destroys it and frees it through its allocator, then empties the list. A null entry faults
//   on its vtable, as it does in the original.
// - frame_timing_task_workers_step_014400a0 (reached through two thunks): three rounds of the
//   worker over its three queues.
// - frame_timing_task_dispatch_0138a370 (the dispatcher's virtual method): when the group record
//   asks for it, wakes every registered task found in the task table; updates the record, fills
//   the job (record, task table, group mask) and runs it like the worker step; closes the record.
// - frame_timing_task_set_frame_value_0143f9f0: copies the frame's value to a global.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "runtime/original.h"

namespace {

// Objects that are only passed on
struct Dispatcher;
struct TaskJob;
struct TaskFrameState;
struct TaskWorker;
struct TaskBatch;
struct TaskTable;
struct Task;
struct FrameInfo {
    uint8_t unknown_0x00[0x8];
    float value;
};
static_assert(offsetof(FrameInfo, value) == 0x8);

// A queued object: run, then destroyed.
struct QueuedObject;
struct QueuedObjectVtable {
    void (*destroy)(QueuedObject *);
    void *unknown_slots[1];
    void (*run)(QueuedObject *);
};
static_assert(offsetof(QueuedObjectVtable, run) == 0x10);
struct QueuedObject {
    const QueuedObjectVtable *vtable;
};

struct QueueLock;
struct QueueLockVtable {
    void *unknown_slots[3];
    void (*lock)(QueueLock *, uint32_t timeout);
    void *unknown_slots_2[1];
    void (*unlock)(QueueLock *);
};
static_assert(offsetof(QueueLockVtable, lock) == 0x18 && offsetof(QueueLockVtable, unlock) == 0x28);
struct QueueLock {
    const QueueLockVtable *vtable;
};
constexpr uint32_t wait_forever = 0xffffffff;

struct ObjectList {
    void *unknown_0x00;
    QueuedObject **begin;
    QueuedObject **end;
};
struct TaskQueue {
    void *unknown_0x00;
    QueueLock *lock;
    ObjectList *list;
};
static_assert(offsetof(ObjectList, end) == 0x10 && offsetof(TaskQueue, list) == 0x10);

// The worker set the run brackets with its two thunks.
constexpr int worker_queue_count = 3;
struct TaskWorkers {
    uint8_t unknown_0x00[0x48];
    TaskWorker *worker;
    TaskBatch *batch;
    TaskJob *queues[worker_queue_count];
};
static_assert(offsetof(TaskWorkers, worker) == 0x48 && offsetof(TaskWorkers, queues) == 0x58);
// The rounds take the queues in this order.
constexpr int worker_round_order[worker_queue_count] = {0, 2, 1};

struct FD4TaskManager {
    uint8_t unknown_0x00[0x10];
    TaskQueue *queue_before;
    TaskQueue *queue_after;
    uint8_t unknown_0x20[0x8];
    TaskWorkers *workers;
    TaskFrameState *frame_state;
    uint8_t running;
    Dispatcher *dispatcher;   // runs the frame's tasks (task_dispatch below)
    TaskTable *task_table;
    TaskWorker *worker;
};
static_assert(offsetof(FD4TaskManager, queue_before) == 0x10 && offsetof(FD4TaskManager, workers) == 0x28);
static_assert(offsetof(FD4TaskManager, frame_state) == 0x30 && offsetof(FD4TaskManager, running) == 0x38);
static_assert(offsetof(FD4TaskManager, dispatcher) == 0x40 && offsetof(FD4TaskManager, task_table) == 0x48);
static_assert(offsetof(FD4TaskManager, worker) == 0x50);

// The dispatcher's records and job.
struct GroupRecord {
    uint8_t unknown_0x00[0x18];
    int32_t count;   // negative: wake the registered tasks first
    uint8_t unknown_0x1c[0x44];
    uint8_t value_0x60;
};
static_assert(offsetof(GroupRecord, count) == 0x18 && offsetof(GroupRecord, value_0x60) == 0x60);
constexpr int group_mask_words = 4;
struct TaskJob {
    uint8_t unknown_0x00[0x8];
    TaskTable *tasks;
    GroupRecord *record;
    uint32_t group_mask;
};
static_assert(offsetof(TaskJob, tasks) == 0x8 && offsetof(TaskJob, record) == 0x10 && offsetof(TaskJob, group_mask) == 0x18);
struct TaskEntry {
    uint32_t id;
    uint8_t unknown_0x04[0x80];
    uint8_t wake_flagged;
};
static_assert(offsetof(TaskEntry, wake_flagged) == 0x84);

struct Dispatcher;
struct DispatcherVtable {
    void *unknown_slots[6];
    void (*dispatch)(Dispatcher *, TaskWorker *, TaskTable *, uint32_t groups, FrameInfo *);
};
static_assert(offsetof(DispatcherVtable, dispatch) == 0x30);
struct Dispatcher {
    const DispatcherVtable *vtable;
    uint8_t unknown_0x08[0x40];
    GroupRecord *record;
    TaskJob *job;
    uint8_t unknown_0x58[0x10];
    TaskBatch *batch;
    uint8_t value_0x70;
};
static_assert(offsetof(Dispatcher, record) == 0x48 && offsetof(Dispatcher, job) == 0x50);
static_assert(offsetof(Dispatcher, batch) == 0x68 && offsetof(Dispatcher, value_0x70) == 0x70);

RT_GLOBAL(0x058b2e30, task_manager_instance, FD4TaskManager *);
RT_GLOBAL(0x04d3b29f, task_manager_name, const char);
RT_GLOBAL(0x0596ccb0, global_0x0596ccb0, const uint64_t);   // read and unused (meaning not known)
RT_GLOBAL(0x058b7e08, frame_value, float);
constexpr uint32_t all_groups = 0xffffffff;

RT_ORIGINAL(0x01388c60, task_run_all, void(FD4TaskManager *, FrameInfo *));
RT_ORIGINAL(0x01388c70, task_run, void(FD4TaskManager *, uint32_t groups, FrameInfo *));
RT_ORIGINAL(0x0143bcc0, frame_state_begin, void(TaskFrameState *, uint64_t frame));
RT_ORIGINAL(0x0143bcd0, frame_state_begin_2, void(TaskFrameState *, uint64_t));
RT_ORIGINAL(0x0143bce0, frame_state_end, void(TaskFrameState *, uint64_t));
RT_ORIGINAL(0x0143e490, queue_flush, void(TaskQueue *));
RT_ORIGINAL(0x01440090, workers_begin, void(TaskWorkers *));
RT_ORIGINAL(0x01440160, workers_end, void(TaskWorkers *));
RT_ORIGINAL(0x0143f9f0, set_frame_value, void(FrameInfo *));
RT_ORIGINAL(0x0247b720, allocator_of, engine::Allocator *(QueuedObject *));
RT_ORIGINAL(0x0143d7c0, batch_select, void(TaskBatch *, TaskJob *));
RT_ORIGINAL(0x0143e030, worker_run, void(TaskWorker *, int32_t, TaskBatch *));
RT_ORIGINAL(0x0143de20, worker_step_a, void(TaskWorker *));
RT_ORIGINAL(0x0143deb0, worker_step_b, void(TaskWorker *));
RT_ORIGINAL(0x0143df10, worker_step_c, void(TaskWorker *));
RT_ORIGINAL(0x01389df0, entry_count, int32_t(Dispatcher *));
RT_ORIGINAL(0x01389e10, entry_at, TaskEntry *(Dispatcher *, int32_t));
RT_ORIGINAL(0x0143f720, task_lookup, Task *(TaskTable *, uint32_t id));
RT_ORIGINAL(0x0143ec40, task_wake, void(Task *));
RT_ORIGINAL(0x0143ed00, task_wake_flagged, void(Task *));
RT_ORIGINAL(0x0143c360, record_update, void(GroupRecord *));
RT_ORIGINAL(0x0143c450, record_close, void(GroupRecord *));
RT_ORIGINAL(0x0143bef0, group_mask_of, void(uint32_t *mask, uint32_t groups));

}  // namespace

extern "C" void bb_frame_timing_task_run_all_01388c60(FD4TaskManager *manager, FrameInfo *frame)
{
    task_run(manager, all_groups, frame);
}

extern "C" void bb_frame_timing_task_set_frame_value_0143f9f0(const FrameInfo *frame)
{
    frame_value.get() = frame->value;
}

extern "C" void bb_frame_timing_task_frame_024512a0(void *, FrameInfo *frame)
{
    (void)*static_cast<const volatile uint64_t *>(global_0x0596ccb0.address());   // read and unused, as the original
    task_run_all(engine::require(task_manager_instance, task_manager_name.address()), frame);
}

extern "C" void bb_frame_timing_task_run_01388c70(FD4TaskManager *manager, uint32_t groups, FrameInfo *frame)
{
    if (manager->running) return;
    frame_state_begin(manager->frame_state, reinterpret_cast<uintptr_t>(frame));
    frame_state_begin_2(manager->frame_state, 0);
    queue_flush(manager->queue_before);
    if (TaskWorkers *workers = manager->workers) workers_begin(workers);
    set_frame_value(frame);
    manager->running = 1;
    if (Dispatcher *dispatcher = manager->dispatcher)
        dispatcher->vtable->dispatch(dispatcher, manager->worker, manager->task_table, groups, frame);
    manager->running = 0;
    queue_flush(manager->queue_after);
    if (TaskWorkers *workers = manager->workers) workers_end(workers);
    frame_state_end(manager->frame_state, 0);
}

extern "C" void bb_frame_timing_task_flush_queue_0143e490(TaskQueue *queue)
{
    if (!queue->list) return;
    QueueLock *lock = queue->lock;
    lock->vtable->lock(lock, wait_forever);
    ObjectList *list = queue->list;
    QueuedObject **it = list->begin;
    if (it != list->end) {
        do {
            QueuedObject *object = *it;
            object->vtable->run(object);
            if (object) {
                engine::Allocator *allocator = allocator_of(object);
                object->vtable->destroy(object);
                allocator->free(object);
            }
            ++it;
            list = queue->list;
        } while (it != list->end);
        it = list->begin;
    }
    list->end = it;   // empty
    lock->vtable->unlock(lock);
}

// Each round reads the worker and the batch again at every call, as the original does.
extern "C" void bb_frame_timing_task_workers_step_014400a0(TaskWorkers *workers)
{
    for (int queue : worker_round_order) {
        batch_select(workers->batch, workers->queues[queue]);
        worker_run(workers->worker, 0, workers->batch);
        worker_step_a(workers->worker);
        worker_step_b(workers->worker);
        worker_step_c(workers->worker);
    }
}

extern "C" void bb_frame_timing_task_dispatch_0138a370(Dispatcher *self, TaskWorker *worker, TaskTable *tasks,
                                                       uint32_t groups)
{
    if (self->record->count < 0) {
        const int32_t count = entry_count(self);
        for (int32_t i = 0; count > 0 && i != count; i++) {
            TaskEntry *entry = entry_at(self, i);
            if (Task *task = task_lookup(tasks, entry->id)) {
                task_wake(task);
                if (entry->wake_flagged) task_wake_flagged(task);
            }
        }
    }
    GroupRecord *record = self->record;
    record->value_0x60 = self->value_0x70;
    record_update(record);
    self->job->record = self->record;
    self->job->tasks = tasks;
    TaskJob *job = self->job;   // read before the mask is computed, as the original
    uint32_t mask[group_mask_words] = {};
    group_mask_of(mask, groups);
    job->group_mask = mask[0];
    batch_select(self->batch, self->job);
    worker_run(worker, 0, self->batch);   // the batch read again, as the original
    worker_step_a(worker);
    worker_step_b(worker);
    worker_step_c(worker);
    record_close(self->record);
}
