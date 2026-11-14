// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: refilling a registered task's queue (FD4::FD4TaskManager's dispatcher).
//
// frame_timing_task_refill_queue_0143ed00 (0x0143ed00, task_wake_flagged in task_manager.cpp,
// which calls it for a registered task whose entry has the flag at +0x84 set, right after it calls
// 0x0143ec40 on the same task): under the task's lock, empties the task's queue (a deque of 16-byte
// items) and frees its storage with 0x0143ec40 (which takes the lock again), then pushes a copy of
// every item of the task's item list onto the back of the queue, first to last, with the deque's
// own push_back (0x02dd7150). The list's ends are read once, before the first push.
//
// It does not wake anything: like 0x0143ec40 (task_wake in task_manager.cpp, which only empties
// the queue), it is named for where the dispatcher calls it.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

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

// An item of the task: 16 bytes, copied as they are.
struct TaskItem {
    uint64_t first;
    uint64_t second;
};
static_assert(sizeof(TaskItem) == 0x10);
// The queue: a std::deque<TaskItem> (its block map at +0x8, start and size at +0x18 and +0x20,
// its allocator at +0x28), only passed on here.
struct ItemQueue {
    uint8_t unknown_0x00[0x30];
};
struct Task {
    uint8_t unknown_0x00[0x38];
    TaskItem *items_begin;   // std::vector<TaskItem>
    TaskItem *items_end;
    uint8_t unknown_0x48[0x10];
    ItemQueue queue;
    uint8_t unknown_0x88[0x8];
    TaskLock lock;
};
static_assert(offsetof(Task, items_begin) == 0x38 && offsetof(Task, items_end) == 0x40);
static_assert(offsetof(Task, queue) == 0x58 && offsetof(Task, lock) == 0x90);

RT_ORIGINAL(0x0143ec40, task_clear_queue, void(Task *));
RT_ORIGINAL(0x02dd7150, queue_push_back, void(ItemQueue *, const TaskItem *));

}  // namespace

extern "C" void bb_frame_timing_task_refill_queue_0143ed00(Task *task)
{
    TaskLock *lock = &task->lock;
    lock->vtable->lock(lock, wait_forever);
    task_clear_queue(task);
    TaskItem *end = task->items_end;
    for (TaskItem *item = task->items_begin; item != end; item++) queue_push_back(&task->queue, item);
    lock->vtable->unlock(lock);
}
