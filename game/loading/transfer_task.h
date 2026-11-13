// SPDX-License-Identifier: GPL-2.0-or-later
// The resource loader's transfer tasks (Dantelion2 TransferTask and its subclass FileTransferTask):
// one task reads one resource. The transfer task manager runs each task's state machine; the
// task records what its read reported in a few status words and finishes in one of its final
// states.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/wide_string.h"
#include "../frame_timing/job.h"
#include "resource_queue.h"
#include "runtime/original.h"

namespace loading::transfer_task {

using resource_queue::LoadRequest;
using resource_queue::ResourceInfo;

// A wide string's text: inline up to 7 characters, else on the heap.
inline const char16_t *wide_text(const engine::WideString *text)
{
    return text->capacity >= engine::wide_string_inline_capacity ? text->heap : text->inline_text;
}

// The task's states (+0x60). A task in one of its final states may not be moved back to a
// working state: such a move is reported ("Tried to shift into lower priority.") and made anyway.
enum TaskState : int32_t {
    state_idle = 0,
    state_reading = 5,        // set by the read step (0x024dde50) when the read went through
    state_started = 6,        // set when a file task starts (0x024d7460)
    state_read_done = 8,
    state_read_done_9 = 9,    // like 8, for a task whose flags have bit 0x80
    state_failed = 0xa,
    state_aborted = 0xb,
};
// The final states (4, 8, 10 and 11) as a bit set over the state number.
constexpr uint32_t final_states = (1u << 4) | (1u << 8) | (1u << 10) | (1u << 11);
constexpr uint32_t final_state_bits = 32;

// The task's result flags (+0x158).
constexpr uint16_t result_read_submitted = 0x0001;
constexpr uint16_t result_finished = 0x0002;
constexpr uint16_t result_closed = 0x0004;        // the finish has run once
constexpr uint16_t result_error_0x10 = 0x0008;    // the open status was 0x10
constexpr uint16_t result_error_0x11 = 0x0010;    // the open status was 0x11
constexpr uint16_t result_flag_0x80 = 0x0080;     // a finished read moves to state 9, not 8
constexpr uint16_t result_error_0x12 = 0x2000;    // the open status was 0x12, or the manager stops

// The open status (+0x74): its value when nothing was reported yet, and the three errors.
constexpr int32_t open_status_none = 0xe;
constexpr int32_t open_status_error_0x10 = 0x10;
constexpr int32_t open_status_error_0x11 = 0x11;
constexpr int32_t open_status_error_0x12 = 0x12;
// The read status (+0x70): a read that went through, and one that has finished.
constexpr int32_t read_status_submitted = 0xc;
constexpr int32_t read_status_finished = 0xd;
// The close status (+0x78): closed, and asked to close.
constexpr int32_t close_status_requested = 1;
constexpr int32_t close_status_closed = 4;
constexpr int32_t failure_code_read = 4;          // the code (+0x64) of a failed read
constexpr int32_t step_status_done = 0xb;         // the two step statuses (+0x68, +0x6c)

// The cancel bits (+0x160), set atomically by the cancel (0x024dd8a0).
constexpr uint32_t cancel_requested = 0x1;

// The chunk flags (+0x12a): bit 3 makes a step wait for the close; the read start sets bit 2 for
// a task holding a read object.
constexpr uint16_t chunk_flag_0x04 = 0x0004;
constexpr uint16_t chunk_wait_for_close = 0x0008;
constexpr uint16_t result_small_read = 0x0400;   // a result flag (+0x158)
constexpr uint8_t task_flag_no_buffer = 0x01;    // bit 0 of the flags at +0x58

// The request's flags byte (LoadRequest +0x40): 0xff while unset; with bit 1 clear the file is
// looked up as a "preferred" location (the device's third argument).
constexpr uint8_t request_flags_unset = 0xff;
constexpr uint8_t request_flag_0x02 = 0x02;

// A file task's read request: what the task's read start (0x024d8470) hands to a reader, copied
// whole into the reader's queue. The reader reports back through the pointers into the task.
struct ReadRequestExtra {               // the request's two texts and its data, when it has data
    const char16_t *text_0xa0;
    const char16_t *text_0xd8;
    const uint8_t *data;
    int32_t value_0x138;
};
static_assert(sizeof(ReadRequestExtra) == 0x20);
struct FileTransferTask;
struct TransferTask;
struct ReadRequest {
    const char16_t *path;
    uint64_t chunk_limit;               // the task's chunk limit before the read
    uint8_t *destination;               // where the read goes on in the task's buffer
    uint64_t size_left;
    uint64_t word_0x20;                 // 0
    uint64_t *task_word_0xb0;
    int32_t *step_status_0x68;
    int32_t *step_status_0x6c;
    int32_t *open_status;
    int32_t close_status;
    int32_t dword_0x80;
    uint8_t *small_buffer;              // the small read's buffer and size, else null and 0
    uint64_t small_size;
    uint64_t buffer_size;               // the small read's buffer size, else 0
    uint64_t word_0x150;
    ReadRequestExtra extra;
    FileTransferTask *task;
    TransferTask *dependent;
    uint64_t *task_word_0x38;
    bool small_read;
    bool byte_0xa9;                     // false
    bool no_buffer;                     // bit 0 of the task's flags at +0x58
};
static_assert(offsetof(ReadRequest, close_status) == 0x48 && offsetof(ReadRequest, extra) == 0x70);
static_assert(offsetof(ReadRequest, task) == 0x90 && offsetof(ReadRequest, small_read) == 0xa8);
static_assert(sizeof(ReadRequest) == 0xb0);

// A transfer reader: the worker a task's reads go to (the manager has two). Its read requests are
// a queue (an engine deque, stored at +0x28 aligned up to 8) guarded by its lock (+0x110); queueing
// one sets its signal (+0xd0). The deque's push_back is 0x024d37d0.
struct ReadRequestQueue;
constexpr size_t read_queue_size = 0x30;   // a word, the block map, its size, the offset, the count, the allocator
constexpr uintptr_t read_queue_alignment = 8;
struct TransferReader {
    uint8_t unknown_0x000[0x28];
    uint8_t read_queue_storage[read_queue_size + read_queue_alignment];
    uint8_t unknown_0x060[0x70];
    resource_queue::ConditionSignal signal;
    frame_timing::job::JobLock lock;
};
static_assert(offsetof(TransferReader, signal) == 0xd0 && offsetof(TransferReader, lock) == 0x110);
RT_ORIGINAL(0x024d37d0, read_request_queue_push_back, void(ReadRequestQueue *, const ReadRequest *));

// The transfer task manager, as far as a task uses it: its two readers (pointers stored at
// +0x5d0, aligned up to 8; a task takes one by where the device finds the file), its lock (+0xe0)
// and its stop flag.
constexpr uintptr_t reader_slot_alignment = 8;
constexpr int reader_count = 2;
struct TransferTaskManager {
    uint8_t unknown_0x000[0xe0];
    frame_timing::job::JobLock lock;
    uint8_t unknown_0x0e8[0x4e8];
    uint8_t reader_storage[reader_count * sizeof(TransferReader *) + reader_slot_alignment];
    uint8_t unknown_0x5e8[0x12c];
    uint8_t flags_0x714;   // bit 0: stop: a file task's start aborts it
    uint8_t unknown_0x715[0x3];
    // Optional callbacks around a file task's read buffer, each with its own context: asked
    // before the buffer is allocated (false refuses it), and told when it is freed or could not
    // be allocated.
    bool (*buffer_allowed)(engine::Allocator *, uint64_t size, uint64_t alignment, void *context);
    void *buffer_allowed_context;
    void (*buffer_released)(engine::Allocator *, uint64_t size, void *context);
    void *buffer_released_context;
};
static_assert(offsetof(TransferTaskManager, lock) == 0xe0 && offsetof(TransferTaskManager, reader_storage) == 0x5d0);
static_assert(offsetof(TransferTaskManager, flags_0x714) == 0x714);
static_assert(offsetof(TransferTaskManager, buffer_allowed) == 0x718 && offsetof(TransferTaskManager, buffer_released) == 0x728);
constexpr uint8_t manager_stopping = 0x01;

// An object the task owns at +0x90 (released when it finishes, 0x02479710).
struct OwnedObject {
    uint8_t unknown_0x00[0x18];
};
RT_ORIGINAL(0x02479710, owned_object_release, void(OwnedObject *));

// An object the task holds at +0x100 until its read finishes (destroyed by its first vtable
// slot, then freed through the allocator that owns it).
struct ReadObject;
struct ReadObjectVtable {
    void (*destroy)(ReadObject *);
};
struct ReadObject {
    const ReadObjectVtable *vtable;
};
RT_ORIGINAL(0x0247b720, allocator_of, engine::Allocator *(ReadObject *));

struct TransferTaskVtable {
    void (*start)(TransferTask *);      // called once a file task has started
    void (*finish)(TransferTask *);     // called once when the task finishes
    void *unknown_slots[6];
    void (*close)(TransferTask *);      // asks for the close
    void *unknown_slots_0x48[2];
    void (*end)(TransferTask *);        // run by the file task's reset (0x024d6850) unless it has ended
};
static_assert(offsetof(TransferTaskVtable, finish) == 0x8 && offsetof(TransferTaskVtable, close) == 0x40);
static_assert(offsetof(TransferTaskVtable, end) == 0x58);

// The object a file task's initialization takes its heap word from when its request has none.
struct HeapWordSource {
    uint8_t unknown_0x000[0x490];
    uint64_t heap_word;
};
static_assert(offsetof(HeapWordSource, heap_word) == 0x490);

struct TransferTask {
    const TransferTaskVtable *vtable;
    uint64_t word_0x08;                 // the initialization's fifth argument: the allocator a file
                                        // task's read buffer comes from (0x024d8210)
    ResourceInfo *info;                 // the resource it loads
    TransferTaskManager *manager;
    TransferReader *reader;             // the reader chosen for the file (also at +0x30)
    frame_timing::job::JobLock *manager_lock;
    TransferReader *read_reader;        // the reader its reads are queued to
    uint64_t word_0x38;
    uint64_t word_0x40;
    uint64_t word_0x48;
    int32_t dword_0x50;
    uint8_t unknown_0x54[0x4];
    uint8_t flags_0x58;                 // bit 0: the file task's last argument
    uint8_t unknown_0x59[0x3];          // zeroed with it
    uint8_t unknown_0x5c[0x4];
    int32_t state;                      // a TaskState
    int32_t failure_code;
    int32_t step_status_0x68;
    int32_t step_status_0x6c;
    int32_t read_status;
    int32_t open_status;
    int32_t close_status;
    int32_t dword_0x7c;
    int32_t dword_0x80;
    uint8_t unknown_0x84[0x4];
    TransferTask *dependent;            // a task made ready once this one's read buffer exists
    OwnedObject owned;
    uint8_t unknown_0x0a8[0x8];
    uint64_t word_0xb0;                 // a read request points at it
    uint8_t unknown_0x0b8[0x10];
    engine::WideString path;            // the path a file task's prepare checks
    ReadObject *read_object;
    uint64_t chunk_size;                // copied to +0x118 when a file task starts, if set
    uint64_t word_0x110;
    uint64_t chunk_limit;               // -1 when initialized
    uint64_t read_size;                 // the size of a file task's read buffer
    uint8_t byte_0x128;
    uint8_t unknown_0x129;
    uint16_t chunk_flags;
    uint8_t unknown_0x12c[0x4];
    uint64_t size_left;                 // what is left to read
    uint64_t size_pending;              // read but not yet counted
    uint64_t file_word_0x140;           // the file task's copy of its request's +0x140
    uint64_t file_word_0x148;           // the file task's copy of its request's +0x20
    uint64_t word_0x150;
    uint16_t result_flags;              // also: 0x200 ended, 0x400 small read, 0x800 buffer allocated
    uint16_t unknown_0x15a;             // zeroed with it
    int32_t ready;                      // 1 once initialized
    int32_t cancel_flags;
    uint8_t unknown_0x164[0x4];
};
static_assert(offsetof(TransferTask, info) == 0x10 && offsetof(TransferTask, reader) == 0x20);
static_assert(offsetof(TransferTask, read_reader) == 0x30 && offsetof(TransferTask, dword_0x50) == 0x50);
static_assert(offsetof(TransferTask, flags_0x58) == 0x58 && offsetof(TransferTask, state) == 0x60);
static_assert(offsetof(TransferTask, read_status) == 0x70 && offsetof(TransferTask, close_status) == 0x78);
static_assert(offsetof(TransferTask, dependent) == 0x88 && offsetof(TransferTask, owned) == 0x90);
static_assert(offsetof(TransferTask, path) == 0xc8 && offsetof(TransferTask, read_size) == 0x120);
static_assert(offsetof(TransferTask, read_object) == 0x100 && offsetof(TransferTask, chunk_limit) == 0x118);
static_assert(offsetof(TransferTask, byte_0x128) == 0x128 && offsetof(TransferTask, chunk_flags) == 0x12a);
static_assert(offsetof(TransferTask, size_left) == 0x130 && offsetof(TransferTask, file_word_0x148) == 0x148);
static_assert(offsetof(TransferTask, result_flags) == 0x158 && offsetof(TransferTask, cancel_flags) == 0x160);

// An object a file task keeps in a chain (+0x2e0) until its reset destroys it (first vtable slot)
// and frees it through the allocator the reset is given.
struct ChainedObject;
struct ChainedObjectVtable {
    void (*destroy)(ChainedObject *);
};
struct ChainedObject {
    const ChainedObjectVtable *vtable;
    uint8_t unknown_0x008[0x140];
    ChainedObject *next;
};
static_assert(offsetof(ChainedObject, next) == 0x148);

// The file transfer task: the request it reads (a copy), the queue that gave it, its read buffer
// (allocated, or the small buffer inside the task) and the lock its update runs under. The file
// load queue keeps them in blocks of 32 slots (0x338 bytes each), free slots chained through +0x330.
constexpr uint64_t small_read_size = 0x2c;
constexpr uint64_t default_read_alignment = 0x80;   // when the request's +0x38 is 0
struct FileTransferTask {
    TransferTask base;
    void *queue;                        // the initialization's second argument
    uint8_t *buffer;                    // the read buffer
    uint64_t buffer_size;
    uint64_t buffer_left;
    engine::Allocator *buffer_allocator; // the allocator of an allocated buffer
    uint8_t owns_buffer;
    uint8_t unknown_0x191[0x7];
    LoadRequest request;
    ChainedObject *chain;
    frame_timing::job::JobLock lock;
    uint8_t unknown_0x2f0[0x10];
    uint8_t small_buffer[small_read_size];
    uint8_t unknown_0x32c[0x4];
    FileTransferTask *next_free;
};
static_assert(offsetof(FileTransferTask, queue) == 0x168 && offsetof(FileTransferTask, owns_buffer) == 0x190);
static_assert(offsetof(FileTransferTask, request) == 0x198 && offsetof(FileTransferTask, lock) == 0x2e8);
static_assert(offsetof(FileTransferTask, small_buffer) == 0x300 && offsetof(FileTransferTask, next_free) == 0x330);
static_assert(sizeof(FileTransferTask) == 0x338);

// The base initialization, and what it asks the disk file device: where it finds the file
// (0 or not), which picks one of the manager's two readers.
RT_ORIGINAL(0x024dd540, transfer_task_initialize,
            bool(TransferTask *, const engine::WideString *path, TransferTaskManager *, ResourceInfo *,
                 uint64_t word_0x08, uint8_t request_flags));
struct DiskFileDevice;
RT_ORIGINAL(0x0246be70, disk_file_device_instance, DiskFileDevice *());
RT_ORIGINAL(0x0246e3d0, disk_file_device_locate, int32_t(DiskFileDevice *, const engine::WideString *path, bool preferred));
constexpr int32_t location_first_reader = 0;

// The file task's initialization (it also runs the base initialization).
RT_ORIGINAL(0x024d6730, file_task_initialize,
            bool(FileTransferTask *, void *queue, const HeapWordSource *heap_words, TransferTaskManager *,
                 ResourceInfo *, const LoadRequest *, uint8_t flag));

// The request's copy assignment (it copies every field, the strings and the data).
RT_ORIGINAL(0x024be630, load_request_assign, LoadRequest *(LoadRequest *, const LoadRequest *source));

// What a file task's start runs first (0x024d8210: false means not yet) and then (0x024d8470,
// with what is left to read).
RT_ORIGINAL(0x024d8210, file_task_prepare, bool(FileTransferTask *));
RT_ORIGINAL(0x024d8470, file_task_begin_read, void(FileTransferTask *, uint64_t size_left));
constexpr int32_t step_status_queued = 3;     // the read step status (+0x68) once a read is queued
constexpr int32_t close_status_3 = 3;         // a read request's close status for a cached info or a held read object
constexpr int32_t state_15 = 15;              // a state in which the small read is not used

// The file task's state handlers, as a table of pointers to member functions (16 bytes each:
// the function, or a vtable offset plus one; then the adjustment to `this`), indexed by state.
// The table is filled when the game starts.
struct MemberFunction {
    uintptr_t function;
    intptr_t this_adjustment;
};
RT_GLOBAL(0x057b4d70, file_task_state_handlers, const MemberFunction);
constexpr uintptr_t member_function_is_virtual = 0x1;
// The update stops in these states: 0, 9, 12, 13 and 14.
constexpr uint32_t update_stop_states = (1u << 0) | (1u << 9) | (1u << 12) | (1u << 13) | (1u << 14);
// An update that starts in state 9 or 14 is reported ("Invalid State...").
constexpr int32_t state_9 = 9;
constexpr int32_t state_14 = 14;

// The assertions' source files, messages and lines.
RT_GLOBAL(0x04d3dd95, transfer_task_source_file, const char);
RT_GLOBAL(0x04d3de0b, lower_priority_message, const char);
constexpr int32_t shift_state_line = 0x3e2;
RT_GLOBAL(0x04d3d9a9, file_transfer_task_source_file, const char);
RT_GLOBAL(0x04d3ddfa, invalid_state_message, const char);
constexpr int32_t update_state_line = 0xd0;

// The file task's prepare (0x024d8210): a check of its path (it returns false in the shipped game)
// and its assertions (the small read's size; the allocator). The original asks an empty check
// (a member of symbols/families/returns_zero.csv) before this one, which therefore never holds.
RT_ORIGINAL(0x02a34320, path_check, bool(const char16_t *path));
RT_GLOBAL(0x04d3da3c, small_read_message, const char);
constexpr int32_t small_read_line = 0x5b6;
RT_GLOBAL(0x04d3da52, no_allocator_message, const char);
constexpr int32_t no_allocator_line = 0x5d0;

// The file task's end (0x024d7e50): the queue reports the loaded request through its first vtable
// slot with code 1 (0x02a692d0); the assertions on the queue and the request's path.
RT_ORIGINAL(0x02a692d0, load_queue_report_loaded, void(void *queue, uint64_t *request_words, uint64_t request_word_0x20));
RT_GLOBAL(0x04d3dd37, no_queue_message, const char);
constexpr int32_t no_queue_line = 0x635;
RT_GLOBAL(0x04d3da26, no_path_message, const char);
constexpr int32_t no_path_line = 0x636;

}  // namespace loading::transfer_task
