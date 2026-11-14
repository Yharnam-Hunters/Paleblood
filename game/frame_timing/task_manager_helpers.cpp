// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: six helpers that the task manager's frame (task_manager.cpp) calls as originals.
// - frame_timing_task_entry_count_01389df0: the number of the dispatcher's registered task entries
//   (a vector of 0x88-byte entries), cut to 32 bits.
// - frame_timing_task_entry_at_01389e10: the entry at an index, or null for a negative index or one
//   past the end.
// - frame_timing_task_batch_add_0143d7c0: pushes a job onto the back of a batch's queue (a deque of
//   job pointers); a null job is not added.
// - frame_timing_task_lookup_0143f720: the task registered under an id in the task table's map, or
//   null when there is none; id 0 is never looked up.
// - frame_timing_task_record_open_0143c360: when the group record is idle, opens it at its first
//   item: index 0, state open, both flags cleared, then one flag set from the kind of that item (when
//   the record has items and the index is in range); the word at +0x64 is cleared.
// - frame_timing_task_record_close_0143c450: when the record is closing, makes it idle again with no
//   index (-1, which makes the next dispatch wake the registered tasks) and both flags cleared.
#include <cstddef>
#include <cstdint>

#include "../engine/msvc_tree.h"
#include "runtime/original.h"

namespace {

// The dispatcher's registered task entries (std::vector<TaskEntry>).
struct TaskEntry {
    uint32_t id;
    uint8_t unknown_0x04[0x80];
    uint8_t wake_flagged;
};
static_assert(sizeof(TaskEntry) == 0x88);
struct Dispatcher {
    uint8_t unknown_0x00[0x18];
    TaskEntry *entries_begin;
    TaskEntry *entries_end;
};
static_assert(offsetof(Dispatcher, entries_begin) == 0x18 && offsetof(Dispatcher, entries_end) == 0x20);

// A batch keeps its jobs in a std::deque<TaskJob *>, pushed by the game's own push_back.
struct TaskJob;
struct TaskBatch;
RT_ORIGINAL(0x02dd5fd0, job_queue_push_back, void(TaskBatch *, TaskJob *const *job));

// The task table maps ids to tasks (std::map<uint32_t, Task *>).
struct Task;
using TaskNode = engine::TreeNode<uint32_t, Task *>;
static_assert(offsetof(TaskNode, is_nil) == 0x19 && offsetof(TaskNode, key) == 0x20 && offsetof(TaskNode, value) == 0x28);
struct TaskTable {
    uint8_t unknown_0x00[0x18];
    TaskNode *head;
};
static_assert(offsetof(TaskTable, head) == 0x18);
constexpr uint32_t no_task_id = 0;

// A group record: the dispatcher opens it before running its job and closes it after.
struct GroupItem {
    uint32_t unknown_0x00;
    uint32_t kind;
};
static_assert(sizeof(GroupItem) == 0x8);
enum RecordState : int32_t { record_idle = 0, record_open = 1, record_closing = 4 };
constexpr uint32_t item_kind_b = 2;   // sets flag_b
constexpr uint32_t item_kind_c = 4;   // sets flag_c
constexpr uint32_t flag_set = 1;
struct GroupRecord {
    uint8_t unknown_0x00[0x8];
    int32_t item_count;
    uint8_t unknown_0x0c[0x4];
    GroupItem *items;
    int32_t index;   // negative: no index (the dispatcher wakes the registered tasks first)
    int32_t state;
    uint32_t flag_b;
    uint32_t flag_c;
    uint8_t unknown_0x28[0x3c];
    int32_t value_0x64;   // cleared on opening (meaning not known)
};
static_assert(offsetof(GroupRecord, item_count) == 0x8 && offsetof(GroupRecord, items) == 0x10);
static_assert(offsetof(GroupRecord, index) == 0x18 && offsetof(GroupRecord, state) == 0x1c);
static_assert(offsetof(GroupRecord, flag_b) == 0x20 && offsetof(GroupRecord, flag_c) == 0x24);
static_assert(offsetof(GroupRecord, value_0x64) == 0x64);

// The record's index is built by two small constructors (too short to replace): the first index
// and no index.
struct RecordIndex {
    int32_t value;
};
RT_ORIGINAL(0x0143bed0, first_index, RecordIndex *(RecordIndex *));
RT_ORIGINAL(0x0143bec0, no_index, RecordIndex *(RecordIndex *));

}  // namespace

extern "C" int32_t bb_frame_timing_task_entry_count_01389df0(const Dispatcher *self)
{
    return static_cast<int32_t>(self->entries_end - self->entries_begin);
}

extern "C" TaskEntry *bb_frame_timing_task_entry_at_01389e10(const Dispatcher *self, int32_t index)
{
    if (index < 0) return nullptr;
    const auto count = static_cast<size_t>(self->entries_end - self->entries_begin);
    if (static_cast<size_t>(index) >= count) return nullptr;
    return self->entries_begin + index;
}

extern "C" void bb_frame_timing_task_batch_add_0143d7c0(TaskBatch *batch, TaskJob *job)
{
    if (job) job_queue_push_back(batch, &job);
}

extern "C" Task *bb_frame_timing_task_lookup_0143f720(const TaskTable *table, uint32_t id)
{
    if (id == no_task_id) return nullptr;
    TaskNode *head = table->head;
    TaskNode *found = engine::tree_find(head, id);
    return found == head ? nullptr : found->value;
}

extern "C" void bb_frame_timing_task_record_open_0143c360(GroupRecord *record)
{
    if (record->state != record_idle) return;
    RecordIndex index;
    record->index = first_index(&index)->value;
    record->state = record_open;
    record->flag_b = 0;
    record->flag_c = 0;
    if (const GroupItem *items = record->items) {
        const int32_t current = record->index;
        if (current >= 0 && current < record->item_count) {
            const uint32_t kind = items[current].kind;
            if (kind == item_kind_c)
                record->flag_c |= flag_set;
            else if (kind == item_kind_b)
                record->flag_b |= flag_set;
        }
    }
    record->value_0x64 = 0;
}

extern "C" void bb_frame_timing_task_record_close_0143c450(GroupRecord *record)
{
    if (record->state != record_closing) return;
    record->flag_b = 0;
    record->flag_c = 0;
    record->state = record_idle;
    RecordIndex index;
    record->index = no_index(&index)->value;
}
