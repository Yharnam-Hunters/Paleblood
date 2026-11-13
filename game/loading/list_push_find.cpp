// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the push_back of two engine pointer arrays and the resource manager's lookup of the
// info for a request's words (structs in resource_queue.h).
//
// loading_transfer_task_list_push_back_02a6b340 (0x02a6b340) appends a transfer task to one of
// the file load queue's transfer task lists (0x02a6a9e0 and 0x02a6ad50 call it), and
// loading_request_pointer_array_push_back_02a6c690 (0x02a6c690) appends a request to the stream
// load queue's array of requests to give back (0x02a6d8e0): two instances of one template. A full
// array first grows to one and a half times its count (engine/container.h) through its own
// reallocation (0x02a6b420, 0x02a6f3d0). A pointer inside the array itself is read again at the
// same index of the new buffer. With no buffer after the growth (a failed allocation that was
// only reported), nothing is stored, but the end still moves on by one pointer, as in the game.
//
// loading_resource_manager_find_info_024c1630 (0x024c1630) walks the manager's slots from the
// last to the first and gives the info of the first one whose three words match the request's
// words 3, 1 and 2 and whose name matches the name word 0 points at; null when none does (or there
// are no slots). The names are compared character by character on each character's low byte
// folded to lower case through the C library's table (_Getptolower, asked for again for every
// character of either name), up to the slot name's end.
#include <cstdint>

#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

using TransferTaskList = engine::Array<TransferTask *>;
RT_ORIGINAL(0x02a6b420, transfer_task_list_reallocate, void(TransferTaskList *, uint64_t capacity));
RT_ORIGINAL(0x02a6f3d0, request_pointer_array_reallocate, void(RequestPointerArray *, uint64_t capacity));

// The C library's lower-case table (an import): one 16-bit entry per byte value.
RT_ORIGINAL(0x02fbec48, c_lower_case_table, const int16_t *());

template <typename T, typename Reallocate>
void push_back(engine::Array<T *> *array, T *const *item, Reallocate grow)
{
    const bool inside_array = array->contains(item);
    if (array->end == array->capacity_end) {
        const uint64_t index = inside_array ? item - array->begin : 0;
        grow(array, engine::array_grown_capacity<T *>(array->count()));
        if (inside_array)
            item = array->begin + index;
    }
    if (array->end)
        *array->end = *item;
    array->end += 1;
}

ResourceSlotArray *aligned_slots(ResourceManager *manager)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(manager->slot_storage);
    return reinterpret_cast<ResourceSlotArray *>((address + slot_storage_alignment - 1) &
                                                 ~(slot_storage_alignment - 1));
}

const char16_t *name_text(const engine::WideString &name)
{
    return name.capacity >= engine::wide_string_inline_capacity ? name.heap : name.inline_text;
}

// A character's low byte through the lower-case table, as a signed byte.
int8_t folded(char16_t character)
{
    return static_cast<int8_t>(c_lower_case_table()[static_cast<uint8_t>(character)]);
}

bool names_match(const char16_t *slot_name, const char16_t *request_name)
{
    for (;; ++slot_name, ++request_name) {
        const int8_t slot_character = folded(*slot_name);
        const int8_t request_character = folded(*request_name);
        if (slot_character != request_character)
            return false;
        if (slot_character == 0)
            return true;
    }
}

constexpr int name_word = 0;
constexpr int slot_word_0x00_match = 3;
constexpr int slot_word_0x08_match = 1;
constexpr int slot_word_0x10_match = 2;

bool slot_matches(const ResourceSlot &slot, const RequestWords &request)
{
    return slot.word_0x00 == request.words[slot_word_0x00_match] &&
           slot.word_0x08 == request.words[slot_word_0x08_match] &&
           slot.word_0x10 == request.words[slot_word_0x10_match] &&
           names_match(name_text(slot.name), reinterpret_cast<const char16_t *>(request.words[name_word]));
}

}  // namespace

extern "C" void bb_loading_transfer_task_list_push_back_02a6b340(TransferTaskList *list, TransferTask *const *task)
{
    push_back(list, task, transfer_task_list_reallocate);
}

extern "C" void bb_loading_request_pointer_array_push_back_02a6c690(RequestPointerArray *array,
                                                                     void *const *request)
{
    push_back(array, request, request_pointer_array_reallocate);
}

extern "C" ResourceInfo *bb_loading_resource_manager_find_info_024c1630(ResourceManager *manager,
                                                                         RequestWords request)
{
    const ResourceSlotArray *slots = aligned_slots(manager);
    for (const ResourceSlot *slot = slots->end; slot != slots->begin;) {
        --slot;
        if (slot_matches(*slot, request))
            return slot->info;
    }
    return nullptr;
}
