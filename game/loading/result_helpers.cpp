// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: small helpers of the load results, the resource handles and a transfer task's owned
// object, which resource_queue.h and transfer_task.h call as originals.
//
// loading_result_construct_024bcfa0 (0x024bcfa0): the result base's constructor: its vtable, the
// four request words, the code and the word.
// loading_result_copy_024bcfe0 (0x024bcfe0): the result base's copy constructor: its vtable, then
// the request words cleared and copied (so copying an entry onto itself clears them), the code and
// the word.
// loading_finished_entry_assign_02aa4b90 (0x02aa4b90): a finished entry's copy assignment: the
// result base's assignment (0x024bd080, every field but the vtable), then the handle's
// assignment. Returns the entry.
// loading_result_assign_024bd080 (0x024bd080): the result base's copy assignment: the four
// request words one by one, the code and the word, each read just before it is written (an entry
// overlapping its source sees its own writes); the vtable, the bytes after the code and the handle
// are left as they were. Returns the entry.
// loading_resource_handle_assign_024bd310 (0x024bd310): a resource handle's copy assignment:
// nothing when both point to the same info; otherwise drops the reference to its info (if any),
// takes the source's info (read again after the drop) and adds a reference to it (if any).
// Returns the handle.
// loading_owned_object_release_02479710 (0x02479710): releases the two objects a transfer task
// owns through +0x90 (the one at +0x10 first, then the one at +0x08): each one is told through
// its vtable slot at +0xb0, then (read again: that call may clear or replace it) destroyed and
// freed through the allocator that owns it, and its slot is cleared.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "resource_queue.h"
#include "runtime/original.h"
#include "transfer_task.h"

using loading::resource_queue::FinishedEntry;
using loading::resource_queue::FinishedEntryVtable;
using loading::resource_queue::RequestWords;
using loading::resource_queue::ResourceHandle;
using loading::resource_queue::ResourceInfo;
using loading::transfer_task::OwnedObject;

namespace {

RT_GLOBAL(0x057b4ad0, result_vtable, const uint8_t);
RT_ORIGINAL(0x024bd080, result_assign, FinishedEntry *(FinishedEntry *, const FinishedEntry *source));
RT_ORIGINAL(0x024bd930, resource_info_release, void(ResourceInfo *));
RT_ORIGINAL(0x02a3a300, resource_info_add_reference, void(ResourceInfo *));

// One of the two objects the owned object holds: destroyed by its first vtable slot after the
// slot at +0xb0 has been called, then freed through the allocator that owns it.
struct OwnedPart;
struct OwnedPartVtable {
    void (*destroy)(OwnedPart *);
    uint8_t unknown_0x08[0xa8];
    void (*before_destroy)(OwnedPart *);
};
static_assert(offsetof(OwnedPartVtable, before_destroy) == 0xb0);
struct OwnedPart {
    const OwnedPartVtable *vtable;
};
RT_ORIGINAL(0x0247b720, allocator_of, engine::Allocator *(OwnedPart *));

struct OwnedParts {
    uint64_t unknown_0x00;
    OwnedPart *part_0x08;
    OwnedPart *part_0x10;
};
static_assert(offsetof(OwnedParts, part_0x08) == 0x08 && offsetof(OwnedParts, part_0x10) == 0x10);
static_assert(sizeof(OwnedParts) == sizeof(OwnedObject));

void set_result_vtable(FinishedEntry *entry)
{
    entry->vtable =
        reinterpret_cast<const FinishedEntryVtable *>(result_vtable.address() + engine::vtable_address_point);
}

void release_part(OwnedPart **slot)
{
    OwnedPart *part = *slot;
    if (part == nullptr)
        return;
    part->vtable->before_destroy(part);
    if (OwnedPart *remaining = *slot) {
        engine::Allocator *allocator = allocator_of(remaining);
        remaining->vtable->destroy(remaining);
        allocator->free(remaining);
    }
    *slot = nullptr;
}

}  // namespace

extern "C" void bb_loading_result_construct_024bcfa0(FinishedEntry *entry, const RequestWords *request_words,
                                                     int32_t code, uint64_t request_word_0x20)
{
    set_result_vtable(entry);
    entry->request_words = *request_words;
    entry->code = code;
    entry->request_word_0x20 = request_word_0x20;
}

extern "C" void bb_loading_result_copy_024bcfe0(FinishedEntry *entry, const FinishedEntry *source)
{
    set_result_vtable(entry);
    entry->request_words = RequestWords{};
    entry->request_words = source->request_words;
    entry->code = source->code;
    entry->request_word_0x20 = source->request_word_0x20;
}

extern "C" FinishedEntry *bb_loading_finished_entry_assign_02aa4b90(FinishedEntry *entry, const FinishedEntry *source)
{
    result_assign(entry, source);
    loading::resource_queue::resource_handle_assign(&entry->handle, &source->handle);
    return entry;
}

extern "C" ResourceHandle *bb_loading_resource_handle_assign_024bd310(ResourceHandle *handle,
                                                                     const ResourceHandle *source)
{
    if (handle->info == source->info)
        return handle;
    if (handle->info != nullptr)
        resource_info_release(handle->info);
    ResourceInfo *info = source->info;
    handle->info = info;
    if (info != nullptr)
        resource_info_add_reference(info);
    return handle;
}

extern "C" void bb_loading_owned_object_release_02479710(OwnedObject *object)
{
    auto *parts = reinterpret_cast<OwnedParts *>(object);
    release_part(&parts->part_0x10);
    release_part(&parts->part_0x08);
}

extern "C" FinishedEntry *bb_loading_result_assign_024bd080(FinishedEntry *entry, const FinishedEntry *source)
{
    for (int i = 0; i < loading::resource_queue::request_word_count; ++i)
        entry->request_words.words[i] = source->request_words.words[i];
    entry->code = source->code;
    entry->request_word_0x20 = source->request_word_0x20;
    return entry;
}
