// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: a file transfer task's read buffer (its prepare), its end and its reset (structs in
// transfer_task.h).
//
// loading_file_task_prepare_024d8210 (0x024d8210), run by the file task's start (0x024d7460):
// first frees a buffer left from an earlier read (an owned one through its allocator, and the
// manager's release callback is told its size) and forgets it. A task with flag 0x58 bit 0 needs
// no buffer (true). A path check (empty in the shipped game; the original asks another empty one
// first) and the close status may also make it true at once: when the check holds, any close
// status other than 0 and 1 does.
// A small read (result flag 0x400 without chunk flag 0x4, the close not done) uses the 0x2c-byte
// buffer inside the task (its read size must be 0x2c). Otherwise the buffer is allocated with the
// read size from the task's allocator (+0x08, asserted), aligned to the request's +0x38 (0x80 when
// 0), after the manager's allowed callback agrees; success sets result flag 0x800 and makes the
// dependent task (+0x88) ready. A refused allocation returns false; a failed one also tells the
// release callback.
//
// loading_file_task_end_024d7e50 (0x024d7e50): once only (result flag 0x200), for a task with an
// info: under the resource manager's lock marks the info (flag 0x40) and hands it to the manager
// (0x024c3260). A task that completed (cancel bit 4 without bit 1) and has no flag 0x58 bit 0
// marks the info loaded (flag 0x01), sets its transfer word to 2 and has the queue report the
// request (0x02a692d0; queue and request path asserted); any other clears the info's flag 0x01
// and sets its word to 2.
//
// loading_file_task_reset_024d6850 (0x024d6850): ends the task through its vtable (slot 0x58)
// unless it has ended or has no info, clears the words +0x08..+0x53 and +0x58, destroys and frees
// (through the given allocator) every object in its chain, and clears the chain, the request's
// heap word and the state. Always true.
#include <cstdint>

#include "../engine/engine.h"
#include "transfer_task.h"

using namespace loading::transfer_task;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;
using loading::resource_queue::ResourceManager;

namespace {

// The task's cancel bits (+0x160) besides cancel_requested.
constexpr uint32_t task_completed = 0x4;   // no cancel is taken after this

// More result flags (+0x158).
constexpr uint16_t result_ended = 0x0200;
constexpr uint16_t result_buffer_allocated = 0x0800;

// The resource info's flag set when its task completed, and the word (+0x10) its end stores.
constexpr uint8_t info_loaded = 0x01;
constexpr int32_t transfer_ended = 2;

ResourceManager *resource_manager()
{
    using namespace loading::resource_queue;
    const auto storage = reinterpret_cast<uintptr_t>(resource_manager_storage.address());
    return reinterpret_cast<ResourceManager *>((storage + resource_manager_alignment - 1) &
                                               ~(resource_manager_alignment - 1));
}

void tell_buffer_released(TransferTaskManager *manager, engine::Allocator *allocator, uint64_t size)
{
    if (manager && manager->buffer_released)
        manager->buffer_released(allocator, size, manager->buffer_released_context);
}

}  // namespace

extern "C" bool bb_loading_file_task_prepare_024d8210(FileTransferTask *file_task)
{
    TransferTask *task = &file_task->base;
    if (file_task->buffer) {
        if (file_task->owns_buffer) {
            file_task->buffer_allocator->free(file_task->buffer);
            tell_buffer_released(task->manager, file_task->buffer_allocator, file_task->buffer_size);
        }
        file_task->buffer = nullptr;
        file_task->buffer_allocator = nullptr;
    }
    if (task->flags_0x58 & task_flag_no_buffer)
        return true;

    if (path_check(wide_text(&task->path)) && task->close_status != 0 && task->close_status != close_status_requested)
        return true;

    if (!(task->chunk_flags & chunk_flag_0x04) && (task->result_flags & result_small_read) &&
        task->close_status != close_status_closed) {
        if (task->read_size != small_read_size)
            engine::fatal_error(file_transfer_task_source_file.address(), small_read_line, small_read_message.address());
        file_task->buffer = file_task->small_buffer;
        file_task->buffer_size = file_task->buffer_left = small_read_size;
        file_task->owns_buffer = 0;
        file_task->buffer_allocator = nullptr;
        return true;
    }

    auto *allocator = reinterpret_cast<engine::Allocator *>(task->word_0x08);
    const uint64_t alignment = file_task->request.word_0x38 ? file_task->request.word_0x38 : default_read_alignment;
    if (!allocator)
        engine::fatal_error(file_transfer_task_source_file.address(), no_allocator_line, no_allocator_message.address());
    file_task->buffer_size = file_task->buffer_left = task->read_size;
    file_task->owns_buffer = 1;
    file_task->buffer_allocator = allocator;
    TransferTaskManager *manager = task->manager;
    if (manager && manager->buffer_allowed &&
        !manager->buffer_allowed(allocator, file_task->buffer_size, alignment, manager->buffer_allowed_context))
        return false;
    file_task->buffer = static_cast<uint8_t *>(allocator->allocate(file_task->buffer_size, alignment));
    if (!file_task->buffer) {
        tell_buffer_released(task->manager, allocator, file_task->buffer_size);
        return false;
    }
    task->result_flags |= result_buffer_allocated;
    if (task->dependent)
        task->dependent->ready = 1;
    return true;
}

extern "C" void bb_loading_file_task_end_024d7e50(FileTransferTask *file_task)
{
    TransferTask *task = &file_task->base;
    if (task->result_flags & result_ended)
        return;
    if (ResourceInfo *info = task->info) {
        ResourceManager *manager = resource_manager();
        JobLock *lock = &manager->lock;
        lock->vtable->lock(lock, wait_forever);
        info->flags |= loading::resource_queue::resource_flag_0x40;
        loading::resource_queue::resource_manager_remove_released(manager, info);
        lock->vtable->unlock(lock);

        const bool completed = (task->cancel_flags & (task_completed | cancel_requested)) == task_completed;
        if (completed && !(task->flags_0x58 & task_flag_no_buffer)) {
            task->info->flags |= info_loaded;
            __atomic_exchange_n(&task->info->transfer_claimed, transfer_ended, __ATOMIC_SEQ_CST);
            if (!file_task->queue)
                engine::fatal_error(file_transfer_task_source_file.address(), no_queue_line, no_queue_message.address());
            if (!file_task->request.word_0x80)
                engine::fatal_error(file_transfer_task_source_file.address(), no_path_line, no_path_message.address());
            load_queue_report_loaded(file_task->queue, &file_task->request.word_0x80, file_task->request.word_0x20);
        } else {
            task->info->flags &= static_cast<uint8_t>(~info_loaded);
            __atomic_exchange_n(&task->info->transfer_claimed, transfer_ended, __ATOMIC_SEQ_CST);
        }
    }
    task->result_flags |= result_ended;
}

extern "C" bool bb_loading_file_task_reset_024d6850(FileTransferTask *file_task, engine::Allocator *allocator)
{
    TransferTask *task = &file_task->base;
    if (task->info && !(task->result_flags & result_ended))
        task->vtable->end(task);
    task->flags_0x58 = 0;
    for (uint8_t &byte : task->unknown_0x59)
        byte = 0;
    task->word_0x08 = 0;
    task->info = nullptr;
    task->manager = nullptr;
    task->reader = nullptr;
    task->manager_lock = nullptr;
    task->read_reader = nullptr;
    task->word_0x38 = task->word_0x40 = task->word_0x48 = 0;
    task->dword_0x50 = 0;

    ChainedObject *object = file_task->chain;
    while (object) {
        ChainedObject *next = object->next;
        object->next = nullptr;
        object->vtable->destroy(object);
        allocator->free(object);
        object = next;
    }
    file_task->chain = nullptr;
    file_task->request.word_0x28 = 0;
    task->state = state_idle;
    return true;
}
