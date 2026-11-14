// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the resource manager's release of a resource info and its removal of a released one
// (structs in resource_queue.h).
//
// loading_resource_manager_release_024c1530 (0x024c1530), called by the resource info's release
// (0x024bd930) once the info has no references left. A manager that was never initialized is
// reported (the report returns, and the release goes on). An info without flag 0x40 is taken out
// of the load event queue it was queued in, if any, and the manager's condition signal is set. Then,
// under the manager's lock, the info is marked released (0x80) and handed to 0x024c3260; the
// unlock is the original's tail call.
//
// loading_resource_manager_remove_released_024c3260 (0x024c3260), run by the release and by the
// file task's end (0x024d7e50), both holding the manager's lock: only for an info with flags 0x40
// and 0x80 (else false). Under the manager's lock it finds the slot holding the info (the first in
// the manager's slot array); with none, false. A slot in use is freed: its name emptied, its first
// three words cleared, its in-use bit cleared, and it is appended to the free slots (an inlined
// push_back that grows the array through 0x024c3160). Then the info is unloaded (0x024bdb80), still
// under the lock. True whenever the slot was found.
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

RT_ORIGINAL(0x024c3160, slot_pointer_array_reallocate, void(ResourceSlotPointerArray *, uint64_t capacity));
RT_ORIGINAL(0x024bdb80, resource_info_unload, void(ResourceInfo *));
RT_GLOBAL(0x04dd85f0, empty_text, const char16_t);

template <typename Array> Array *aligned_array(uint8_t *storage)
{
    const auto address = reinterpret_cast<uintptr_t>(storage);
    return reinterpret_cast<Array *>((address + slot_storage_alignment - 1) & ~(slot_storage_alignment - 1));
}

ResourceSlot *find_slot(const ResourceSlotArray *slots, const ResourceInfo *info)
{
    for (ResourceSlot *slot = slots->begin; slot != slots->end; ++slot) {
        if (slot->info == info)
            return slot;
    }
    return nullptr;
}

void push_free_slot(ResourceSlotPointerArray *free_slots, ResourceSlot *slot)
{
    if (free_slots->end == free_slots->capacity_end)
        slot_pointer_array_reallocate(free_slots, engine::array_grown_capacity<ResourceSlot *>(free_slots->count()));
    if (free_slots->end)
        *free_slots->end = slot;
    free_slots->end += 1;
}

void free_slot(ResourceManager *manager, ResourceSlot *slot)
{
    wide_string_assign_counted(&slot->name, empty_text.address(), 0);
    slot->word_0x08 = 0;
    slot->word_0x10 = 0;
    slot->word_0x00 = 0;
    slot->flags &= ~resource_slot_in_use;
    push_free_slot(aligned_array<ResourceSlotPointerArray>(manager->free_slot_storage), slot);
}

}  // namespace

extern "C" void bb_loading_resource_manager_release_024c1530(ResourceManager *manager, ResourceInfo *info)
{
    if ((manager->flags & resource_manager_initialized) == 0)
        engine::fatal_error(resource_manager_source_file.address(), resource_manager_release_line,
                            resource_manager_not_initialized.address());
    if ((info->flags & resource_flag_0x40) == 0) {
        if (info->load_queue)
            load_queue_remove(info->load_queue, info);
        condition_signal_set(&manager->signal, true);
    }
    JobLock *lock = &manager->lock;
    lock->vtable->lock(lock, wait_forever);
    info->flags |= resource_released;
    resource_manager_remove_released(manager, info);
    lock->vtable->unlock(lock);
}

extern "C" bool bb_loading_resource_manager_remove_released_024c3260(ResourceManager *manager, ResourceInfo *info)
{
    if ((info->flags & resource_flag_0x40) == 0 || (info->flags & resource_released) == 0)
        return false;
    JobLock *lock = &manager->lock;
    lock->vtable->lock(lock, wait_forever);
    ResourceSlot *slot = find_slot(aligned_array<ResourceSlotArray>(manager->slot_storage), info);
    if (slot && (slot->flags & resource_slot_in_use)) {
        free_slot(manager, slot);
        resource_info_unload(info);
    }
    lock->vtable->unlock(lock);
    return slot != nullptr;
}
