// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: putting a task into the task manager's run queues, and adding the manager's
// switch to the debug menu.
//
// A task either waits on a pair of other objects (then it is handed to the waiting list) or goes
// straight into the queue of its group, optionally ordered after another task. The group is also
// kept in three bits of the task's flags.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct TaskQueue;
struct DebugMenuNode;

constexpr uint8_t task_state_waiting = 2;
constexpr uint8_t group_shift = 2;
constexpr uint8_t group_mask = 0x7 << group_shift;   // bits 2 to 4 of the flags byte

struct Task {
    uint8_t unknown_0x00[0x78];
    uint8_t state;
    uint8_t flags;   // the group in bits 2 to 4; the rest is kept
};
static_assert(offsetof(Task, state) == 0x78 && offsetof(Task, flags) == 0x79);

constexpr size_t group_count = 8;   // the three group bits
struct TaskManager {
    uint8_t unknown_0x00[0xd8];
    TaskQueue *queues[group_count];
};
static_assert(offsetof(TaskManager, queues) == 0xd8);

RT_ORIGINAL(0x013e7170, task_wait_for, void(Task *task, void *first, void *second));
RT_ORIGINAL(0x01435b80, queue_add, void(TaskQueue *queue, Task *task));
RT_ORIGINAL(0x01435cc0, queue_order_after, void(TaskQueue *queue, Task *before, Task *task));

// The object whose debug menu entry 0x013894f0 adds.
struct DebugSwitchOwner {
    uint8_t unknown_0x00[0x30];
    void *task_manager;   // passed to the empty function after the entry is added
    uint8_t unknown_0x38;
    bool debug_switch;   // the menu entry's value
    uint8_t unknown_0x3a[0x1e];
    DebugMenuNode *menu;  // the "Task" node, once added
};
static_assert(offsetof(DebugSwitchOwner, task_manager) == 0x30 && offsetof(DebugSwitchOwner, debug_switch) == 0x39);
static_assert(offsetof(DebugSwitchOwner, menu) == 0x58);

RT_ORIGINAL(0x0140a410, debug_menu_add_node, DebugMenuNode *(DebugMenuNode *parent, const char16_t *name));
RT_ORIGINAL(0x01407f60, debug_menu_add_switch,
            void(DebugMenuNode *node, const char16_t *name, bool *value, int32_t count));
// The UTF-16 names: "Task", the settings node under it, and the switch.
RT_GLOBAL(0x04cb70d8, menu_name_task, const char16_t);
RT_GLOBAL(0x04cb70e2, menu_name_settings, const char16_t);
RT_GLOBAL(0x04cb70e8, menu_name_switch, const char16_t);
constexpr int32_t one_switch = 1;

}  // namespace

// Schedules `task` in group `group` (0 to 7, unchecked). With both `first` and `second` set, the
// task waits for them instead (state 2) and nothing else changes. `unused` is not read.
extern "C" void bb_frame_timing_task_manager_add_01381580(TaskManager *manager, void *unused, Task *task, void *first,
                                                          void *second, uint32_t group, Task *after)
{
    (void)unused;
    if (first != nullptr && second != nullptr) {
        task->state = task_state_waiting;
        task_wait_for(task, first, second);
        return;
    }
    // The original updates the four bytes from +0x79 at once; only the group bits change.
    task->flags = static_cast<uint8_t>((task->flags & ~group_mask) | ((group << group_shift) & group_mask));
    queue_add(manager->queues[group], task);
    if (after != nullptr)
        queue_order_after(manager->queues[group], after, task);
}

// Adds the "Task" node under `parent` (once: not when `parent` is null or the node exists) and a
// settings node under it with the switch. The original then passes the task manager and the node
// to an empty function (a member of the returns-nothing leaf family, compiled out in this build);
// that call is left out.
extern "C" void bb_frame_timing_task_debug_menu_add_013894f0(DebugSwitchOwner *owner, DebugMenuNode *parent)
{
    if (parent == nullptr || owner->menu != nullptr)
        return;
    owner->menu = debug_menu_add_node(parent, menu_name_task.address());
    DebugMenuNode *settings = debug_menu_add_node(owner->menu, menu_name_settings.address());
    debug_menu_add_switch(settings, menu_name_switch.address(), &owner->debug_switch, one_switch);
}
