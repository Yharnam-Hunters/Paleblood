// SPDX-License-Identifier: GPL-2.0-or-later
// The resource manager's load events and file load slots (Dantelion2 DLResourceLoadEventQueue,
// DLResourceFileLoadEventQueue) and the resource info's release (DLResourceInfo).
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/container.h"
#include "../engine/wide_string.h"
#include "../frame_timing/job.h"
#include "runtime/original.h"

namespace loading::resource_queue {

using engine::WideString;

// The request a load event carries: a polymorphic object with three wide strings and a data
// array of its own (its constructors: 0x024be250, copy 0x024be3b0). What each string holds is
// not known yet.
struct LoadRequest {
    const uint8_t *vtable;
    uint64_t word_0x08;
    uint64_t word_0x10;
    uint64_t word_0x18;
    uint64_t word_0x20;          // the constructor's third argument
    uint64_t word_0x28;
    uint64_t word_0x30;
    uint64_t word_0x38;
    uint8_t byte_0x40;           // 0xff when constructed
    uint8_t byte_0x41;
    uint8_t unknown_0x42[0x6];
    WideString name;             // the constructor's text
    uint64_t word_0x80;
    uint64_t word_0x88;
    uint64_t word_0x90;
    uint64_t word_0x98;
    WideString text_0xa0;
    WideString text_0xd8;
    engine::Array<uint8_t> data;
    int32_t value_0x138;
    uint8_t unknown_0x13c[0x4];
    uint64_t word_0x140;
};
static_assert(offsetof(LoadRequest, word_0x20) == 0x20 && offsetof(LoadRequest, byte_0x40) == 0x40);
static_assert(offsetof(LoadRequest, name) == 0x48 && offsetof(LoadRequest, word_0x98) == 0x98);
static_assert(offsetof(LoadRequest, text_0xa0) == 0xa0 && offsetof(LoadRequest, text_0xd8) == 0xd8);
static_assert(offsetof(LoadRequest, data) == 0x110 && offsetof(LoadRequest, value_0x138) == 0x138);
static_assert(offsetof(LoadRequest, word_0x140) == 0x140 && sizeof(LoadRequest) == 0x148);

// A queued load event: two words copied as they are, then the request.
struct LoadEvent {
    uint64_t word_0x00;
    uint64_t word_0x08;
    LoadRequest request;
};
static_assert(offsetof(LoadEvent, request) == 0x10 && sizeof(LoadEvent) == 0x158);

using LoadEventArray = engine::Array<LoadEvent>;
static_assert(sizeof(LoadEventArray) == 0x28);

// A queued stream load event (DLResourceStreamLoadEventQueue): a load event and one more byte.
struct StreamLoadEvent {
    LoadEvent event;
    uint8_t byte_0x158;
    uint8_t unknown_0x159[0x7];
};
static_assert(offsetof(StreamLoadEvent, byte_0x158) == 0x158 && sizeof(StreamLoadEvent) == 0x160);
using StreamLoadEventArray = engine::Array<StreamLoadEvent>;

// The request's copy constructor (it leaves the base class's vtable), and the vtables: the
// queued request's, which a copy in the queue gets, and the base class's, which the destructor
// puts back.
RT_ORIGINAL(0x024be3b0, load_request_copy, void(LoadRequest *, const LoadRequest *source));
RT_GLOBAL(0x056b6d30, queued_request_vtable, const uint8_t);
RT_GLOBAL(0x056b6d60, base_request_vtable, const uint8_t);
RT_GLOBAL(0x056b6dc0, stream_request_vtable, const uint8_t);   // a copy in the stream queue gets it

// The request's last part, from its second string on, which 0x024be8e0 builds: both strings and
// the data array on the default container heap.
struct LoadRequestTail {
    WideString text_0xa0;
    WideString text_0xd8;
    engine::Array<uint8_t> data;
};
static_assert(offsetof(LoadRequestTail, text_0xd8) == offsetof(LoadRequest, text_0xd8) - offsetof(LoadRequest, text_0xa0));
static_assert(offsetof(LoadRequestTail, data) == offsetof(LoadRequest, data) - offsetof(LoadRequest, text_0xa0));

// The request's second string and its data (0x024be8e0), and the heap requests take their name
// string from (an allocator inside the object 0x0247bbf0 returns).
RT_ORIGINAL(0x024be8e0, load_request_construct_tail, void(WideString *text_0xa0));
// The engine array's copy assignment, for the request's data (0x02b4d100, a template instance).
RT_ORIGINAL(0x02b4d100, byte_array_assign, engine::Array<uint8_t> *(engine::Array<uint8_t> *, const engine::Array<uint8_t> *source));
RT_ORIGINAL(0x0247bbf0, request_heap_owner, uint8_t *());
constexpr ptrdiff_t request_heap_offset = 0x458;
// The wide string's assign from counted text (it allows text inside the string itself).
RT_ORIGINAL(0x02a2a720, wide_string_assign_counted, WideString *(WideString *, const char16_t *text, uint64_t length));
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));

RT_ORIGINAL(0x02a6bc50, load_event_array_reallocate, void(LoadEventArray *, uint64_t capacity));
RT_ORIGINAL(0x02a6f140, stream_event_array_reallocate, void(StreamLoadEventArray *, uint64_t capacity));

// One of the file load event queue's slots (0x338 bytes), built by its constructor.
struct FileLoadSlot {
    uint8_t unknown_0x000[0x338];
};
constexpr int file_load_slot_count = 32;
struct FileLoadSlots {
    FileLoadSlot slots[file_load_slot_count];
};
static_assert(sizeof(FileLoadSlots) == 0x6700);
RT_ORIGINAL(0x024d63f0, file_load_slot_construct, void(FileLoadSlot *));

// A request still waiting on a resource: a node of the resource info's pending list (an
// MSVC-style circular list with a head node).
struct PendingNode {
    PendingNode *next;
    PendingNode *previous;
    void *request;
};
static_assert(offsetof(PendingNode, request) == 0x10);

struct StreamLoadQueue;
struct LoadEventQueue;

struct ResourceInfo {
    uint8_t unknown_0x00[0x10];
    int32_t transfer_claimed;     // set to 1 (an atomic exchange) when a transfer task takes it
    uint8_t unknown_0x14[0x1d];
    uint8_t flags;
    uint8_t unknown_0x32[0x36];
    LoadEventQueue *load_queue;   // the load event queue it was queued in (0x02a698e0), if any
    StreamLoadQueue *stream_queue;
    uint8_t unknown_0x78[0x8];
    PendingNode *pending_head;
    uint64_t pending_count;
    engine::Allocator *pending_allocator;
};
static_assert(offsetof(ResourceInfo, transfer_claimed) == 0x10);
static_assert(offsetof(ResourceInfo, flags) == 0x31 && offsetof(ResourceInfo, stream_queue) == 0x70);
static_assert(offsetof(ResourceInfo, load_queue) == 0x68);
static_assert(offsetof(ResourceInfo, pending_head) == 0x80 && offsetof(ResourceInfo, pending_allocator) == 0x90);

constexpr uint8_t resource_releasing = 0x10;          // the release has started (set once)
constexpr uint8_t resource_pending_cancelled = 0x20;  // its pending requests were taken away
constexpr uint8_t resource_flag_0x40 = 0x40;          // while clear, a release takes it out of its file queue
constexpr uint8_t resource_released = 0x80;           // set by the manager's release

// The resource info's reference count: drop one (not below 0) and read, each under the info's
// own lock.
RT_ORIGINAL(0x02a3a2a0, resource_info_drop_reference, void(ResourceInfo *));
RT_ORIGINAL(0x02a3a1b0, resource_info_reference_count, int32_t(ResourceInfo *));

// The resource manager (its storage is a static, aligned up to 8 where it is used) and its
// release of a resource info.
RT_GLOBAL(0x05aa4590, resource_manager_storage, uint8_t);
constexpr uintptr_t resource_manager_alignment = 8;

// A condition signal (Dantelion2 DLConditionSignal) and its set (0x0247e780: under its mutex,
// set or clear the signalled bit and wake the waiters).
struct ConditionSignal {
    uint8_t unknown_0x00[0x40];
};
RT_ORIGINAL(0x0247e780, condition_signal_set, uint64_t(ConditionSignal *, bool signalled));

// The resource manager (Dantelion2 DLResourceManagerImpl), as far as its release uses it.
struct ResourceManager {
    uint8_t flags;   // bit 0: initialized
    uint8_t unknown_0x001[0x647];
    frame_timing::job::JobLock lock;
    uint8_t unknown_0x650[0x50];
    ConditionSignal signal;   // a release of an info outside the 0x40 state sets it
};
static_assert(offsetof(ResourceManager, lock) == 0x648 && offsetof(ResourceManager, signal) == 0x6a0);
constexpr uint8_t resource_manager_initialized = 0x01;
RT_ORIGINAL(0x024c1530, resource_manager_release, void(ResourceManager *, ResourceInfo *));

// The release's assertion: the source file, line and message ("DLResourceManager" and "not
// initialized" in Japanese) it reports when the manager was never initialized.
RT_GLOBAL(0x04d3d566, resource_manager_source_file, const char);
RT_GLOBAL(0x04d3d5d4, resource_manager_not_initialized, const char);
constexpr int32_t resource_manager_release_line = 0x2e6;

// The file load queue's removal of a resource info, and what the manager does next with a
// released info (0x024c3260, not analyzed beyond its checks of flags 0x40 and 0x80).
RT_ORIGINAL(0x02a6af30, load_queue_remove, void(LoadEventQueue *, ResourceInfo *));
RT_ORIGINAL(0x024c3260, resource_manager_remove_released, uint64_t(ResourceManager *, ResourceInfo *));

// The stream load queue (Dantelion2 DLResourceStreamLoadEventQueue), as far as the removal of a
// request uses it: requests live in a pool (+0xc8, +0xd0 bytes); the requests in use and the
// requests to give back are pointer arrays stored at +0x108 and +0xd8, each aligned up to 8.
using RequestPointerArray = engine::Array<void *>;
constexpr uintptr_t request_storage_alignment = 8;
constexpr size_t request_storage_size = sizeof(RequestPointerArray) + request_storage_alignment;
struct LoadEventQueueVtable;
constexpr uintptr_t stream_event_storage_alignment = 8;
constexpr size_t stream_event_storage_size = sizeof(StreamLoadEventArray) + stream_event_storage_alignment;
struct StreamLoadQueue {
    const LoadEventQueueVtable *vtable;   // its first slot reports a request it could not queue
    uint64_t unknown_0x08;
    ResourceManager *manager;             // the original reads it through a one-line getter
    uint8_t unknown_0x18[0x60];
    uint8_t event_storage[stream_event_storage_size];
    frame_timing::job::JobLock lock;
    uint8_t unknown_0xb0[0x18];
    uint8_t *pool;
    uint64_t pool_size;
    uint8_t released_storage[request_storage_size];
    uint8_t in_use_storage[request_storage_size];
};
static_assert(offsetof(StreamLoadQueue, manager) == 0x10 && offsetof(StreamLoadQueue, event_storage) == 0x78);
static_assert(offsetof(StreamLoadQueue, lock) == 0xa8);
static_assert(offsetof(StreamLoadQueue, pool) == 0xc8 && offsetof(StreamLoadQueue, released_storage) == 0xd8);
static_assert(offsetof(StreamLoadQueue, in_use_storage) == 0x108);

// The stream load queue's removal of a request, the push_back of the array of requests to give
// back (0x02a6c690, a template instance), and the removal's assertion: a request outside the pool
// ("Given Pointer is Out Of Range...").
RT_ORIGINAL(0x02a6d8e0, stream_queue_remove, void(StreamLoadQueue *, void *request));
RT_ORIGINAL(0x02a6c690, request_pointer_array_push_back, void(RequestPointerArray *, void *const *request));
RT_ORIGINAL(0x02fbe658, c_memmove, void *(void *, const void *, uint64_t));
RT_GLOBAL(0x04c6be45, stream_queue_source_file, const char);
RT_GLOBAL(0x04c6bebc, out_of_range_message, const char);
constexpr int32_t stream_queue_remove_line = 0xe3;

// The request's destructor (inlined where a request is destroyed): the base class's vtable back,
// its data buffer freed, its three strings released and emptied, last declared first.
inline void empty_string(WideString *s)
{
    engine::wide_string_release(s);
    s->capacity = engine::wide_string_inline_capacity - 1;
    s->size = 0;
    s->inline_text[0] = u'\0';
}

inline void destroy_request(LoadRequest *request)
{
    request->vtable = base_request_vtable.address() + engine::vtable_address_point;
    engine::Array<uint8_t> &data = request->data;
    if (data.begin) {
        data.allocator->free(data.begin);
        data.capacity_end = nullptr;
        data.end = nullptr;
        data.begin = nullptr;
    }
    empty_string(&request->text_0xd8);
    empty_string(&request->text_0xa0);
    empty_string(&request->name);
}

// What the request's own vtable answers first: whether it can be loaded.
struct LoadRequestVtable {
    void *unknown_slots[2];
    bool (*is_valid)(const LoadRequest *);
};
static_assert(offsetof(LoadRequestVtable, is_valid) == 0x10);
inline bool request_is_valid(const LoadRequest *request)
{
    return reinterpret_cast<const LoadRequestVtable *>(request->vtable)->is_valid(request);
}

// A reference to a resource info (its constructor takes a reference, 0x024bd470; its destructor
// drops it, 0x024bd4c0). The load queue returns its subclass, with that class's vtable.
struct ResourceHandle {
    const uint8_t *vtable;
    ResourceInfo *info;
};
static_assert(sizeof(ResourceHandle) == 0x10);
RT_ORIGINAL(0x024bd470, resource_handle_construct, void(ResourceHandle *, ResourceInfo *));
RT_ORIGINAL(0x024bd4c0, resource_handle_destroy, void(ResourceHandle *));
RT_GLOBAL(0x057b4af0, load_handle_vtable, const uint8_t);

// The stream queue's request: a load request and one more byte, which its queued event keeps.
struct StreamLoadRequest {
    LoadRequest request;
    uint8_t byte_0x148;
};
static_assert(offsetof(StreamLoadRequest, byte_0x148) == 0x148);

// The stream queue's handle: a resource handle that also knows the queue (its constructor
// 0x02a70a90 makes the info remember the queue, +0x70, when it has both).
struct StreamHandle {
    ResourceHandle handle;
    StreamLoadQueue *queue;
};
static_assert(offsetof(StreamHandle, queue) == 0x10 && sizeof(StreamHandle) == 0x18);
RT_GLOBAL(0x056b6f90, stream_handle_vtable, const uint8_t);
RT_ORIGINAL(0x02a70a90, stream_handle_construct, void(StreamHandle *, ResourceInfo *, StreamLoadQueue *));
RT_ORIGINAL(0x02a6d010, stream_event_array_push_back, void(StreamLoadEventArray *, const StreamLoadEvent *));
constexpr int32_t stream_event_kind = 0;

// The resource load event queue (Dantelion2 DLResourceLoadEventQueue and its file load
// subclass): its first vtable slot reports a request it could not queue; its events (an array
// stored at +0x70 aligned up to 8) are guarded by the lock at +0xa0. The file load queue keeps its
// transfer tasks at +0x28.
struct LoadEventQueue;
struct LoadEventQueueVtable {
    void (*report_failure)(LoadEventQueue *, uint64_t *request_words, int32_t code, uint64_t request_word_0x20);
};
struct TransferTasks;
constexpr uintptr_t event_storage_alignment = 8;
constexpr size_t event_storage_size = sizeof(LoadEventArray) + event_storage_alignment;
struct LoadEventQueue {
    const LoadEventQueueVtable *vtable;
    uint64_t unknown_0x08;
    ResourceManager *manager;   // the original reads it through a one-line getter
    uint8_t unknown_0x18[0x10];
    TransferTasks *transfer_tasks;
    uint8_t unknown_0x30[0x40];
    uint8_t event_storage[event_storage_size];
    frame_timing::job::JobLock lock;
};
static_assert(offsetof(LoadEventQueue, manager) == 0x10 && offsetof(LoadEventQueue, transfer_tasks) == 0x28);
static_assert(offsetof(LoadEventQueue, event_storage) == 0x70 && offsetof(LoadEventQueue, lock) == 0xa0);
constexpr int32_t request_not_acquired = -0xf8;   // the code reported when no info could be made

// The kinds of load event the queue's two request functions queue.
constexpr int32_t load_event_kind_0 = 0;   // 0x02a698e0
constexpr int32_t load_event_kind_4 = 4;   // 0x02a69cd0

// The manager's info for a request (made or found, 0x024c17e0; null when it has none).
RT_ORIGINAL(0x024c17e0, resource_manager_info_for, ResourceInfo *(ResourceManager *, const LoadRequest *));
RT_ORIGINAL(0x02a69b70, load_event_array_push_back, void(LoadEventArray *, const LoadEvent *));

// A transfer task (Dantelion2 TransferTask) reading a resource: +0x10 is the info it loads. Its
// cancel (0x024dd8a0) sets a bit in its state atomically.
struct TransferTask {
    uint8_t unknown_0x00[0x10];
    ResourceInfo *info;
};
static_assert(offsetof(TransferTask, info) == 0x10);
RT_ORIGINAL(0x024dd8a0, transfer_task_cancel, uint64_t(TransferTask *, bool));

// The file load queue's transfer tasks: six lists, searched in order, under a read-write lock
// (Dantelion2 DLReadWriteLock: read lock with a timeout 0x0247f640, unlock 0x0247f710).
struct ReadWriteLock {
    uint8_t unknown_0x00[0x10];
};
constexpr int transfer_task_list_count = 6;
struct TransferTasks {
    uint8_t unknown_0x00[0x18];
    engine::Array<TransferTask *> lists[transfer_task_list_count];
    ReadWriteLock lock;
};
static_assert(offsetof(TransferTasks, lists) == 0x18 && offsetof(TransferTasks, lock) == 0x108);
RT_ORIGINAL(0x0247f640, read_write_lock_read, uint64_t(ReadWriteLock *, int32_t timeout));
RT_ORIGINAL(0x0247f710, read_write_lock_unlock, uint64_t(ReadWriteLock *));
// The removal's assertion: no transfer tasks ("Invalid Parameter...").
RT_GLOBAL(0x04c6bd3c, load_queue_source_file, const char);
RT_GLOBAL(0x04c6bdd3, invalid_parameter_message, const char);
constexpr int32_t load_queue_remove_line = 0x247;

// A queue of finished entries kept by an object of the load event queue's code (0x02a69fb0 copies
// its front, 0x02a6a020 drops it): an MSVC-style list (head node, count, allocator) stored at +0x30
// aligned up to 8, guarded by the lock at +0x58. What owns it and what an entry holds beyond its
// two parts (a job result, then a second part at +0x38; copied by 0x02aa4b90) is not known yet.
struct FinishedEntry;
struct FinishedEntryVtable {
    void (*destroy)(FinishedEntry *);
};
struct FinishedEntry {
    const FinishedEntryVtable *vtable;
};
struct FinishedNode {
    FinishedNode *next;
    FinishedNode *previous;
    FinishedEntry entry;
};
static_assert(offsetof(FinishedNode, entry) == 0x10);
struct FinishedList {
    uint64_t unknown_0x00;
    FinishedNode *head;
    uint64_t count;
    engine::Allocator *allocator;
};
static_assert(offsetof(FinishedList, count) == 0x10 && sizeof(FinishedList) == 0x20);
constexpr uintptr_t finished_storage_alignment = 8;
struct FinishedQueue {
    uint8_t unknown_0x00[0x30];
    uint8_t finished_storage[sizeof(FinishedList) + finished_storage_alignment];
    frame_timing::job::JobLock lock;
};
static_assert(offsetof(FinishedQueue, finished_storage) == 0x30 && offsetof(FinishedQueue, lock) == 0x58);
RT_ORIGINAL(0x02aa4b90, finished_entry_copy, FinishedEntry *(FinishedEntry *, const FinishedEntry *source));

}  // namespace loading::resource_queue
