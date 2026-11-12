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

// The request's copy constructor (it leaves the base class's vtable), and the vtables: the
// queued request's, which a copy in the queue gets, and the base class's, which the destructor
// puts back.
RT_ORIGINAL(0x024be3b0, load_request_copy, void(LoadRequest *, const LoadRequest *source));
RT_GLOBAL(0x056b6d30, queued_request_vtable, const uint8_t);
RT_GLOBAL(0x056b6d60, base_request_vtable, const uint8_t);

// The request's second string and its data (0x024be8e0), and the heap requests take their name
// string from (an allocator inside the object 0x0247bbf0 returns).
RT_ORIGINAL(0x024be8e0, load_request_construct_tail, void(WideString *text_0xa0));
RT_ORIGINAL(0x0247bbf0, request_heap_owner, uint8_t *());
constexpr ptrdiff_t request_heap_offset = 0x458;
// The wide string's assign from counted text (it allows text inside the string itself).
RT_ORIGINAL(0x02a2a720, wide_string_assign_counted, WideString *(WideString *, const char16_t *text, uint64_t length));
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));

RT_ORIGINAL(0x02a6bc50, load_event_array_reallocate, void(LoadEventArray *, uint64_t capacity));

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

struct ResourceInfo {
    uint8_t unknown_0x00[0x31];
    uint8_t flags;
    uint8_t unknown_0x32[0x3e];
    StreamLoadQueue *stream_queue;
    uint8_t unknown_0x78[0x8];
    PendingNode *pending_head;
    uint64_t pending_count;
    engine::Allocator *pending_allocator;
};
static_assert(offsetof(ResourceInfo, flags) == 0x31 && offsetof(ResourceInfo, stream_queue) == 0x70);
static_assert(offsetof(ResourceInfo, pending_head) == 0x80 && offsetof(ResourceInfo, pending_allocator) == 0x90);

constexpr uint8_t resource_releasing = 0x10;          // the release has started (set once)
constexpr uint8_t resource_pending_cancelled = 0x20;  // its pending requests were taken away

// The resource info's reference count: drop one (not below 0) and read, each under the info's
// own lock.
RT_ORIGINAL(0x02a3a2a0, resource_info_drop_reference, void(ResourceInfo *));
RT_ORIGINAL(0x02a3a1b0, resource_info_reference_count, int32_t(ResourceInfo *));

// The resource manager (its storage is a static, aligned up to 8 where it is used) and its
// release of a resource info.
RT_GLOBAL(0x05aa4590, resource_manager_storage, uint8_t);
constexpr uintptr_t resource_manager_alignment = 8;
struct ResourceManager;
RT_ORIGINAL(0x024c1530, resource_manager_release, void(ResourceManager *, ResourceInfo *));

// The stream load queue's removal of a request.
RT_ORIGINAL(0x02a6d8e0, stream_queue_remove, void(StreamLoadQueue *, void *request));

}  // namespace loading::resource_queue
