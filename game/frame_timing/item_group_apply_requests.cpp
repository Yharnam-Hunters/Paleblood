// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: an item group takes its items' requested enabled states.
//
// frame_timing_item_update_013d4810 (task_part_items.cpp) calls this on an item's group before it
// looks at the item itself, so every item of the group sees its requested state as its enabled
// state from then on; switching an item on or off is left to that caller. The item vector's two
// ends are read once, and the count is the vector's length in pointers taken as a signed 32-bit
// number, so a length of 2^31 pointers or more (or an end below the start) updates nothing, and
// only the low 32 bits of a longer one count. Each item pointer is read again from the vector for
// every item, after the previous item's write.
//
// The item and group are prefix views of the same objects task_part_items.cpp describes (Item,
// ItemGroup); that file keeps its own definitions so its verification rows stay as they are.
#include <cstddef>
#include <cstdint>

namespace {

struct GroupItem {
    uint8_t unknown_0x00[0x68];
    uint8_t enabled;
    uint8_t enable_requested;
};
static_assert(offsetof(GroupItem, enabled) == 0x68 && offsetof(GroupItem, enable_requested) == 0x69);

struct ItemGroup {
    uint8_t unknown_0x00[0x20];
    GroupItem **begin;
    GroupItem **end;
};
static_assert(offsetof(ItemGroup, begin) == 0x20 && offsetof(ItemGroup, end) == 0x28);

constexpr unsigned pointer_size_shift = 3;

}  // namespace

extern "C" void bb_frame_timing_item_group_apply_requests(ItemGroup *group)
{
    GroupItem **items = group->begin;
    const uintptr_t bytes = reinterpret_cast<uintptr_t>(group->end) - reinterpret_cast<uintptr_t>(items);
    const int32_t count = static_cast<int32_t>(static_cast<uint32_t>(bytes >> pointer_size_shift));
    for (int32_t i = 0; i < count; i++) {
        GroupItem *item = items[i];
        item->enabled = item->enable_requested;
    }
}
