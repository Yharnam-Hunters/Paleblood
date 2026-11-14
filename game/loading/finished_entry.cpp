// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the load event queue's finished entries (their constructor, copy and destructor), the
// queue's post of one, and its report of a loaded request (structs in resource_queue.h).
//
// loading_finished_entry_construct_02aa4a30 (0x02aa4a30): builds the result base from the four
// request words, the code and the word, then a handle to the info the manager finds for those
// words, with the load handle's vtable.
// loading_finished_entry_copy_construct_02aa4ab0 (0x02aa4ab0): copies the result base, starts an
// empty load handle and assigns the source's handle to it (the original tail-calls the assign).
// loading_finished_entry_destroy_02aa4b40 (0x02aa4b40): drops the handle. The original then
// tail-calls the result base's destructor, an empty function (a member of
// symbols/families/returns_nothing.csv), which is left out.
//
// loading_finished_queue_post_02a6b0e0 (0x02a6b0e0): under the queue's lock, builds an entry on
// the stack for the request words, code and word (with the queue's manager), appends a copy to the
// list and destroys the local one. Always true.
//
// loading_load_queue_report_loaded_02a692d0 (0x02a692d0): reports a request through the queue's
// first vtable slot with code 1 (a tail call).
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

constexpr int32_t report_loaded = 1;

void set_entry_vtables(FinishedEntry *entry)
{
    entry->vtable = reinterpret_cast<const FinishedEntryVtable *>(finished_entry_vtable.address() +
                                                                  engine::vtable_address_point);
}

const uint8_t *load_handle_vtable_address()
{
    return load_handle_vtable.address() + engine::vtable_address_point;
}

}  // namespace

extern "C" void bb_loading_finished_entry_construct_02aa4a30(FinishedEntry *entry, const RequestWords *request_words,
                                                             int32_t code, uint64_t request_word_0x20,
                                                             ResourceManager *manager)
{
    result_construct(entry, request_words, code, request_word_0x20);
    set_entry_vtables(entry);
    resource_handle_construct(&entry->handle, resource_manager_find_info(manager, *request_words));
    entry->handle.vtable = load_handle_vtable_address();
}

extern "C" void bb_loading_finished_entry_copy_construct_02aa4ab0(FinishedEntry *entry, const FinishedEntry *source)
{
    result_copy(entry, source);
    set_entry_vtables(entry);
    entry->handle.info = nullptr;
    entry->handle.vtable = load_handle_vtable_address();
    resource_handle_assign(&entry->handle, &source->handle);
}

extern "C" void bb_loading_finished_entry_destroy_02aa4b40(FinishedEntry *entry)
{
    set_entry_vtables(entry);
    resource_handle_destroy(&entry->handle);
}

extern "C" bool bb_loading_finished_queue_post_02a6b0e0(FinishedQueue *queue, const RequestWords *request_words,
                                                        int32_t code, uint64_t request_word_0x20)
{
    JobLock *lock = &queue->lock;
    lock->vtable->lock(lock, wait_forever);
    const auto storage = reinterpret_cast<uintptr_t>(queue->finished_storage);
    auto *list = reinterpret_cast<FinishedList *>((storage + finished_storage_alignment - 1) &
                                                  ~(finished_storage_alignment - 1));
    FinishedEntry entry;
    finished_entry_construct(&entry, request_words, code, request_word_0x20, queue->manager);
    finished_list_push_back(list, &entry);
    finished_entry_destroy(&entry);
    lock->vtable->unlock(lock);
    return true;
}

extern "C" void bb_loading_load_queue_report_loaded_02a692d0(LoadEventQueue *queue, uint64_t *request_words,
                                                             uint64_t request_word_0x20)
{
    queue->vtable->report_failure(queue, request_words, report_loaded, request_word_0x20);
}
