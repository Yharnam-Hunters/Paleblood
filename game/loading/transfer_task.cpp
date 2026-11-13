// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: a transfer task's initialization and its read and close steps, and the file transfer
// task's initialization, start, read step and update (structs in transfer_task.h).
//
// loading_transfer_task_initialize_024dd540 (0x024dd540), called by the file task's
// initialization (0x024d6730): clears the task's state and status words, keeps the manager, the
// info and a word, asks the disk file device where it finds the path (the request's flags decide
// the device's third argument) and takes the manager's first heap when that answer is 0, the
// second otherwise. It claims the info (an atomic exchange of its +0x10 with 1) and marks itself
// ready. Always true.
//
// loading_file_task_initialize_024d6730 (0x024d6730), called by the transfer task manager: copies
// the request into the task (a request without a heap word, +0x28, keeps the word the task had);
// a copy that is not valid makes it return false with nothing else changed. Otherwise it runs the
// base initialization with the request's path (+0x80), its heap word (or, when it has none, the
// word at +0x490 of the third argument) and its flags byte (+0x40), then keeps the queue, the last
// argument in bit 0 of the flags at +0x58, and the request's words +0x140 and +0x20.
//
// The steps return whether the manager may go on with the task: true, unless the task holds a
// read object, waits for the close (chunk flag 3) and is not closed yet.
// - loading_transfer_task_check_read_024dde50 (0x024dde50): a submitted read marks the task
//   (result 0x1) and moves it to reading, or aborts it when it was cancelled; an open error (0x10,
//   0x11, 0x12) is noted in the result flags and aborts it; a task whose open status still says
//   nothing goes on; any other status fails it with code 4.
// - loading_transfer_task_check_close_024ddf20 (0x024ddf20): once the read has finished, frees the
//   read object; a task that is closed and not cancelled is asked to close (read status back to 0);
//   otherwise it is marked finished, and the first time also runs its finish slot, releases what it
//   owns at +0x90 and goes idle.
// - loading_file_task_check_read_024d6e20 (0x024d6e20): the file task's version: open errors abort
//   it, as does a cancel; otherwise, when nothing is pending or both step statuses are done, the
//   pending size is taken from what is left and the read is done (state 9 for result flag 0x80,
//   else 8).
// - loading_file_task_start_024d7460 (0x024d7460): a stopping manager aborts the task (true); a
//   cancelled one is aborted too (false); a task that is not ready, or not prepared yet (0x024d8210),
//   returns false. Otherwise the chunk size (if set) becomes the chunk limit, the read begins
//   (0x024d8470) with what is left, the task moves to started and its start slot runs.
// - loading_file_task_update_024d6950 (0x024d6950), the manager's per-task update: under the task's
//   lock, runs the handler of the current state from the handler table until a stop state, a
//   handler that leaves the state as it was, or state 0. An update entered in state 9 or 14 is
//   reported first. Always true.
//
// Moving to a working state (5, 6 or 9) from a final state (4, 8, 10, 11) is reported and done
// anyway; moves to 8, 10 and 11 are never reported.
#include <cstdint>

#include "../engine/engine.h"
#include "transfer_task.h"

using namespace loading::transfer_task;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

void shift_to_working_state(TransferTask *task, TaskState state)
{
    const auto current = static_cast<uint32_t>(task->state);
    if (current < final_state_bits && (final_states >> current & 1))
        engine::fatal_error(transfer_task_source_file.address(), shift_state_line, lower_priority_message.address());
    task->state = state;
}

// Whether the manager may go on with the task after a step.
constexpr uint8_t task_flag_from_file_argument = 0x01;   // bit 0 of the flags at +0x58
constexpr uint32_t bit_test_modulo = 63;                  // a 64-bit bit test uses the low 6 bits

bool may_continue(const TransferTask *task)
{
    if (!task->read_object || !(task->chunk_flags & chunk_wait_for_close))
        return true;
    return task->close_status == close_status_closed;
}

// An open error noted in the result flags, or 0.
uint16_t open_error_flag(int32_t open_status)
{
    switch (open_status) {
    case open_status_error_0x12: return result_error_0x12;
    case open_status_error_0x10: return result_error_0x10;
    case open_status_error_0x11: return result_error_0x11;
    default: return 0;
    }
}

engine::Allocator *heap_slot(TransferTaskManager *manager, int index)
{
    const auto storage = reinterpret_cast<uintptr_t>(manager->heap_storage);
    const uintptr_t first = (storage + heap_slot_alignment - 1) & ~(heap_slot_alignment - 1);
    return reinterpret_cast<engine::Allocator *const *>(first)[index];
}

}  // namespace

extern "C" bool bb_loading_transfer_task_initialize_024dd540(TransferTask *task, const engine::WideString *path,
                                                             TransferTaskManager *manager, ResourceInfo *info,
                                                             uint64_t word_0x08, uint8_t request_flags)
{
    task->word_0x08 = word_0x08;
    task->info = info;
    task->manager = manager;
    task->word_0x38 = task->word_0x40 = task->word_0x48 = 0;
    task->dword_0x50 = 0;
    task->flags_0x58 = 0;
    for (uint8_t &byte : task->unknown_0x59)
        byte = 0;
    task->state = state_idle;
    task->failure_code = 0;
    task->step_status_0x68 = 0;
    task->read_status = 0;
    task->open_status = open_status_none;
    task->close_status = 0;
    task->dword_0x7c = task->dword_0x80 = 0;
    task->word_0x88 = 0;
    task->read_object = nullptr;
    task->chunk_size = task->word_0x110 = 0;
    task->chunk_limit = UINT64_MAX;
    task->word_0x120 = 0;
    task->byte_0x128 = 0;
    task->chunk_flags = 0;
    task->size_left = task->size_pending = 0;
    task->word_0x150 = 0;
    task->result_flags = 0;
    task->unknown_0x15a = 0;
    task->cancel_flags = 0;

    const bool preferred = request_flags == request_flags_unset || !(request_flags & request_flag_0x02);
    const int32_t location = disk_file_device_locate(disk_file_device_instance(), path, preferred);
    engine::Allocator *heap = heap_slot(manager, location == location_first_heap ? 0 : 1);
    task->heap = heap;
    task->manager_lock = &manager->lock;
    task->buffer_heap = heap;
    __atomic_exchange_n(&info->transfer_claimed, 1, __ATOMIC_SEQ_CST);
    task->ready = 1;
    return true;
}

extern "C" bool bb_loading_file_task_initialize_024d6730(FileTransferTask *task, void *queue,
                                                         const HeapWordSource *heap_words,
                                                         TransferTaskManager *manager, ResourceInfo *info,
                                                         const LoadRequest *request, uint8_t flag)
{
    if (request->word_0x28) {
        load_request_assign(&task->request, request);
    } else {
        const uint64_t kept = task->request.word_0x28;
        load_request_assign(&task->request, request);
        task->request.word_0x28 = kept;
    }
    if (!loading::resource_queue::request_is_valid(&task->request))
        return false;

    const uint64_t heap_word = task->request.word_0x28 ? task->request.word_0x28 : heap_words->heap_word;
    transfer_task_initialize(&task->base, reinterpret_cast<const engine::WideString *>(request->word_0x80), manager,
                             info, heap_word, request->byte_0x40);
    task->queue = queue;
    task->base.flags_0x58 = (task->base.flags_0x58 & ~task_flag_from_file_argument) | flag;
    task->byte_0x190 = 0;
    task->word_0x170 = task->word_0x178 = task->word_0x180 = task->word_0x188 = 0;
    task->base.file_word_0x140 = request->word_0x140;
    task->base.file_word_0x148 = request->word_0x20;
    task->base.state = state_idle;
    return true;
}

extern "C" bool bb_loading_transfer_task_check_read_024dde50(TransferTask *task)
{
    if (task->read_status == read_status_submitted) {
        task->result_flags |= result_read_submitted;
        if (task->cancel_flags & cancel_requested)
            task->state = state_aborted;
        else
            shift_to_working_state(task, state_reading);
        return true;
    }
    if (const uint16_t error = open_error_flag(task->open_status)) {
        task->result_flags |= error;
        task->state = state_aborted;
        return true;
    }
    if (task->open_status != open_status_none) {
        task->failure_code = failure_code_read;
        task->state = state_failed;
    }
    return true;
}

extern "C" bool bb_loading_transfer_task_check_close_024ddf20(TransferTask *task)
{
    if (task->read_status != read_status_finished)
        return true;
    if (ReadObject *object = task->read_object) {
        engine::Allocator *allocator = allocator_of(object);
        object->vtable->destroy(object);
        allocator->free(object);
        task->read_object = nullptr;
    }
    if (task->close_status == close_status_closed && !(task->cancel_flags & cancel_requested)) {
        task->close_status = close_status_requested;
        task->read_status = 0;
        task->vtable->close(task);
        return true;
    }
    task->result_flags |= result_finished;
    if (!(task->result_flags & result_closed)) {
        task->vtable->finish(task);
        owned_object_release(&task->owned);
        task->state = state_idle;
        task->result_flags |= result_closed;
    }
    return true;
}

extern "C" bool bb_loading_file_task_check_read_024d6e20(FileTransferTask *file_task)
{
    TransferTask *task = &file_task->base;
    if (const uint16_t error = open_error_flag(task->open_status)) {
        task->result_flags |= error;
        task->state = state_aborted;
        return true;
    }
    if (task->cancel_flags & cancel_requested) {
        task->state = state_aborted;
        return true;
    }
    const bool steps_done = task->step_status_0x68 == step_status_done && task->step_status_0x6c == step_status_done;
    if (task->size_pending == 0 || steps_done) {
        task->size_left -= task->size_pending;
        task->size_pending = 0;
        file_task->word_0x180 = 0;
        if (task->result_flags & result_flag_0x80)
            shift_to_working_state(task, state_read_done_9);
        else
            task->state = state_read_done;
    }
    return may_continue(task);
}

extern "C" bool bb_loading_file_task_start_024d7460(FileTransferTask *file_task)
{
    TransferTask *task = &file_task->base;
    if (task->manager->flags_0x714 & manager_stopping) {
        task->result_flags |= result_error_0x12;
        task->state = state_aborted;
        return true;
    }
    if (task->cancel_flags & cancel_requested) {
        task->state = state_aborted;
        return false;
    }
    if (!task->ready || !file_task_prepare(file_task))
        return false;
    if (task->chunk_size)
        task->chunk_limit = task->chunk_size;
    file_task_begin_read(file_task, task->size_left);
    shift_to_working_state(task, state_started);
    task->vtable->start(task);
    return may_continue(task);
}

extern "C" bool bb_loading_file_task_update_024d6950(FileTransferTask *file_task)
{
    TransferTask *task = &file_task->base;
    JobLock *lock = &file_task->lock;
    lock->vtable->lock(lock, wait_forever);
    if (task->state == state_9 || task->state == state_14)
        engine::fatal_error(file_transfer_task_source_file.address(), update_state_line, invalid_state_message.address());

    // the bit test takes the state modulo 64, as the original's does
    while (!(uint64_t{update_stop_states} >> (static_cast<uint32_t>(task->state) & bit_test_modulo) & 1)) {
        const int32_t before = task->state;
        const MemberFunction &handler = file_task_state_handlers.address()[static_cast<uint32_t>(task->state)];
        auto *target = reinterpret_cast<uint8_t *>(file_task) + handler.this_adjustment;
        uintptr_t function = handler.function;
        if (function & member_function_is_virtual) {
            const auto vtable = *reinterpret_cast<const uint8_t *const *>(target);
            function = *reinterpret_cast<const uintptr_t *>(vtable + function - member_function_is_virtual);
        }
        reinterpret_cast<void (*)(void *)>(function)(target);
        if (task->state == before || task->state == state_idle)
            break;
    }
    lock->vtable->unlock(lock);
    return true;
}
