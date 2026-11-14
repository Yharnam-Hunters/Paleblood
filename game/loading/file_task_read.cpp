// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: a file transfer task's read start (structs in transfer_task.h).
//
// loading_file_task_begin_read_024d8470 (0x024d8470), run by the file task's start (0x024d7460)
// with what is left to read: builds a read request and queues it to the task's reader. The request
// carries the task's path, its chunk limit (then reset to -1), where the read goes on in the task's
// buffer, the size left (also kept at +0x138), pointers to the task's status words, the small
// read's buffer (a task with chunk flag 0x4 and result flag 0x400; not in state 15), and, for a
// request with bit 0 of its byte +0x41, its two texts and data (only when its +0x138 is set: the
// original leaves those words unset otherwise). The second step status becomes done (0xb).
// A task that holds a read object, has no chunk flag 0x4 and is not closed gets that flag and a
// request with close status 3 and no dependent; any other carries the task's close status (3
// instead when its info's read flags, read under the shared job lock, are set and have bit 0) and
// its dependent task. Under the reader's lock the first step status becomes queued (3, an atomic
// exchange after a full fence), the request is queued and the reader's signal is set.
#include <cstdint>

#include "transfer_task.h"

using namespace loading::transfer_task;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;
using loading::resource_queue::LoadRequest;

namespace {

constexpr uint8_t request_has_extra = 0x01;   // bit 0 of the request's byte +0x41
constexpr uint8_t read_flags_unset = 0xff;
constexpr uint8_t read_flag_keep_open = 0x01;

ReadRequestQueue *read_queue(TransferReader *reader)
{
    const auto storage = reinterpret_cast<uintptr_t>(reader->read_queue_storage);
    return reinterpret_cast<ReadRequestQueue *>((storage + read_queue_alignment - 1) & ~(read_queue_alignment - 1));
}

uint8_t info_read_flags(const ResourceInfo *info)
{
    JobLock *lock = frame_timing::job::job_lock.address();
    lock->vtable->lock(lock, wait_forever);
    const uint8_t flags = info->read_flags;
    lock->vtable->unlock(lock);
    return flags;
}

}  // namespace

extern "C" void bb_loading_file_task_begin_read_024d8470(FileTransferTask *file_task, uint64_t size_left)
{
    TransferTask *task = &file_task->base;
    ReadRequest request;
    request.task = file_task;
    if ((task->chunk_flags & chunk_flag_0x04) && (task->result_flags & result_small_read)) {
        request.buffer_size = file_task->buffer_size;
        request.small_read = true;
        request.small_buffer = file_task->small_buffer;
        request.small_size = small_read_size;
    } else {
        request.small_read = false;
        request.buffer_size = 0;
        request.small_size = 0;
        request.small_buffer = nullptr;
    }
    if (task->state == state_15)
        request.small_read = false;

    task->step_status_0x6c = step_status_done;
    request.destination = file_task->buffer + (file_task->buffer_size - file_task->buffer_left);
    request.size_left = size_left;
    request.chunk_limit = task->chunk_limit;
    request.step_status_0x68 = &task->step_status_0x68;
    request.step_status_0x6c = &task->step_status_0x6c;
    request.open_status = &task->open_status;
    request.task_word_0xb0 = &task->word_0xb0;
    request.path = wide_text(&task->path);
    request.byte_0xa9 = false;
    request.word_0x150 = task->word_0x150;
    request.no_buffer = task->flags_0x58 & task_flag_no_buffer;
    request.task_word_0x38 = &task->word_0x38;

    const LoadRequest &load = file_task->request;
    if (!(load.byte_0x41 & request_has_extra))
        request.extra = {};
    else if (load.value_0x138)
        request.extra = {wide_text(&load.text_0xa0), wide_text(&load.text_0xd8), load.data.begin, load.value_0x138};

    if (task->read_object && !(task->chunk_flags & chunk_flag_0x04) && task->close_status != close_status_closed) {
        task->chunk_flags |= chunk_flag_0x04;
        request.close_status = close_status_3;
        request.dependent = nullptr;
    } else {
        const uint8_t read_flags = info_read_flags(task->info);
        const bool keep_open = read_flags != read_flags_unset && (read_flags & read_flag_keep_open);
        request.close_status = keep_open ? close_status_3 : task->close_status;
        request.dependent = task->dependent;
    }
    request.dword_0x80 = task->dword_0x80;
    task->chunk_limit = UINT64_MAX;
    task->size_pending = size_left;

    TransferReader *reader = task->read_reader;
    reader->lock.vtable->lock(&reader->lock, wait_forever);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_exchange_n(&task->step_status_0x68, step_status_queued, __ATOMIC_SEQ_CST);
    request.word_0x20 = 0;
    read_request_queue_push_back(read_queue(reader), &request);
    loading::resource_queue::condition_signal_set(&reader->signal, true);
    reader->lock.vtable->unlock(&reader->lock);
}
