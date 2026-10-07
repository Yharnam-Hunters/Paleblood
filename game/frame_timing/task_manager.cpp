// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the task manager's frame (FD4::FD4TaskManager, singleton 0x058b2e30).
//
// The frame step (0x02418d20) runs the frame's tasks through 0x024512a0 -> 0x01388c60 ->
// 0x01388c70, which brackets the dispatch with setup and teardown.
// - frame_timing_task_frame_024512a0 (0x024512a0): the frame step's entry: the task manager
//   singleton (a missing one reports through the engine's fatal error and is read again), then
//   0x01388c60(manager, frame). Its first argument is not used.
// - frame_timing_task_run_all_01388c60 (0x01388c60): runs the tasks of every group (-1).
// - frame_timing_task_run_01388c70 (0x01388c70): unless the manager is already running (+0x38),
//   prepares the frame (0x0143bcc0 and 0x0143bcd0 on +0x30, 0x0143e490 on +0x10, 0x01440090 on
//   +0x28 when set, 0x0143f9f0 on the frame), runs the dispatcher (+0x40, virtual +0x30 with +0x50,
//   +0x48, the group mask and the frame) with +0x38 set, then finishes (0x0143e490 on +0x18,
//   0x01440160 on +0x28 when set, 0x0143bce0 on +0x30).
// - frame_timing_task_flush_queue_0143e490 (0x0143e490): when the queue has a list (+0x10), locks
//   it (+0x8, virtual +0x18 with -1), runs every queued object (virtual +0x10; a null entry is
//   called too, as the original does), destroys it (virtual +0x0) and frees it through the
//   allocator 0x0247b720 finds for it (virtual +0x70), empties the list (end = begin) and unlocks
//   (virtual +0x28).
// - frame_timing_task_workers_step_014400a0 (0x014400a0; reached through the thunks 0x01440090
//   and 0x01440160): three rounds over the worker queues +0x58, +0x68 and +0x60 of
//   0x0143d7c0(+0x50, queue), 0x0143e030(+0x48, 0, +0x50), 0x0143de20, 0x0143deb0 and
//   0x0143df10 on +0x48.
// - frame_timing_task_dispatch_0138a370 (0x0138a370; the dispatcher's virtual +0x30, called by
//   0x01388c70 with the manager's +0x50 and +0x48): when the group record (+0x48) has a negative
//   count (+0x18), wakes every registered task found in the task table (0x01389df0 counts the
//   entries, 0x01389e10 gives each, 0x0143f720 looks its id up; 0x0143ec40, and 0x0143ed00 when
//   the entry's +0x84 is set); copies +0x70 to the record's +0x60 and updates it (0x0143c360);
//   fills the job at +0x50 (record, task table, the group mask through 0x0143bef0) and runs it
//   like the worker step (0x0143d7c0, 0x0143e030, 0x0143de20, 0x0143deb0, 0x0143df10); then
//   closes the record (0x0143c450).
// - frame_timing_task_set_frame_value_0143f9f0 (0x0143f9f0): copies the float at +0x8 of the
//   frame's information to the global 0x058b7e08.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "runtime/guest.h"
#include "runtime/recorder.hpp"

namespace {

constexpr uint32_t task_run = 0x01388c70, task_run_all = 0x01388c60;
constexpr uint32_t frame_begin_a = 0x0143bcc0, frame_begin_b = 0x0143bcd0, frame_end = 0x0143bce0, queue_flush = 0x0143e490,
                   worker_begin = 0x01440090, worker_end = 0x01440160, set_frame_value = 0x0143f9f0;
constexpr uint32_t fatal_error = 0x024b55b0, task_manager = 0x058b2e30, debug_flag = 0x0596ccb0;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd, str_task_manager = 0x04d3b29f;
constexpr uint32_t frame_value = 0x058b7e08;
constexpr uint32_t allocator_of = 0x0247b720;
constexpr uint32_t entry_count = 0x01389df0, entry_at = 0x01389e10, task_lookup = 0x0143f720, task_wake = 0x0143ec40,
                   task_wake_flagged = 0x0143ed00, record_update = 0x0143c360, record_close = 0x0143c450, group_mask = 0x0143bef0;
constexpr uint32_t workers_select = 0x0143d7c0, workers_run = 0x0143e030, workers_a = 0x0143de20, workers_b = 0x0143deb0,
                   workers_c = 0x0143df10;

using TaskRun = void(void *manager, uint32_t groups, void *frame);
using TaskRunAll = void(void *manager, void *frame);
using Void1 = void(void *);
using Void2 = void(void *, void *);
using Begin = void(void *, uint64_t);
using Dispatch = void(void *, void *, void *, uint32_t, void *);
using Fatal = void(const char *, int, const char *, const char *, ...);
template <typename T> T get(const void *p, unsigned off) { T v; std::memcpy(&v, static_cast<const char *>(p) + off, sizeof v); return v; }

std::string hex64(const void *p)
{
    char b[24];
    std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)));
    return b;
}

}  // namespace

extern "C" void bb_frame_timing_task_run_all_01388c60(void *manager, void *frame)
{
    rt::Recorder rec;
    rec.begin("frame_timing_task_run_all_01388c60");
    rt::fn<TaskRun>(task_run)(manager, 0xffffffffu, frame);
    if (rec.on()) {
        rec.stub("{\"address\": \"0x01388c70\", \"argc\": 3}");
        rec.write("0x01388c60", "\"rdi\": \"" + hex64(manager) + "\", \"rsi\": \"" + hex64(frame) + "\"");
    }
}

extern "C" void bb_frame_timing_task_set_frame_value_0143f9f0(const unsigned char *frame)
{
    rt::Recorder rec;
    if (rec.begin("frame_timing_task_set_frame_value_0143f9f0")) rec.bytes(frame, 0x8, 4);
    std::memcpy(rt::ptr<unsigned char>(frame_value), frame + 0x8, 4);
    rec.write("0x0143f9f0", "\"rdi\": \"buf:" + (rec.on() ? rec.name(frame) : std::string()) + "\"");
}

extern "C" void bb_frame_timing_task_frame_024512a0(void *, void *frame)
{
    rt::Recorder rec;
    rec.begin("frame_timing_task_frame_024512a0");
    (void)*reinterpret_cast<volatile uint64_t *>(rt::ptr<uint64_t>(debug_flag));   // read and unused, as the original
    void *manager = *rt::ptr<void *>(task_manager);
    if (rec.on()) rec.global_pointer(task_manager);
    if (!manager) {
        rt::fn<Fatal>(fatal_error)(rt::ptr<char>(str_singleton_header), 0xb1, rt::ptr<char>(str_singleton_func),
                                   rt::ptr<char>(str_task_manager));
        if (rec.on()) rec.stub("{\"address\": \"0x024b55b0\", \"argc\": 4}");
        manager = *rt::ptr<void *>(task_manager);
    }
    rt::fn<TaskRunAll>(task_run_all)(manager, frame);
    if (rec.on()) {
        rec.stub("{\"address\": \"0x01388c60\", \"argc\": 2}");
        rec.write("0x024512a0", "\"rdi\": \"0x0\", \"rsi\": \"" + hex64(frame) + "\"");
    }
}

extern "C" void bb_frame_timing_task_run_01388c70(unsigned char *manager, uint32_t groups, void *frame)
{
    rt::Recorder rec;
    if (rec.begin("frame_timing_task_run_01388c70")) rec.bytes(manager, 0x38, 1);
    auto stub = [&](const char *address, int argc) {
        if (rec.on()) rec.stub(std::string("{\"address\": \"") + address + "\", \"argc\": " + std::to_string(argc) + "}");
    };
    auto finish = [&] {
        if (rec.on())
            rec.write("0x01388c70", "\"rdi\": \"buf:" + rec.name(manager) + "\", \"rsi\": \"" + std::to_string(groups) +
                                        "\", \"rdx\": \"" + hex64(frame) + "\"");
    };
    if (manager[0x38]) return finish();
    if (rec.on()) {
        for (unsigned off : {0x10u, 0x18u, 0x28u, 0x30u, 0x48u, 0x50u}) rec.bytes(manager, off, 8);
        rec.pointer(manager, 0x40);
    }
    rt::fn<Begin>(frame_begin_a)(get<void *>(manager, 0x30), reinterpret_cast<uintptr_t>(frame));
    rt::fn<Begin>(frame_begin_b)(get<void *>(manager, 0x30), 0);
    rt::fn<Void1>(queue_flush)(get<void *>(manager, 0x10));
    stub("0x0143e490", 1);
    if (void *workers = get<void *>(manager, 0x28)) {
        rt::fn<Void1>(worker_begin)(workers);
        stub("0x01440090", 1);
    }
    rt::fn<Void1>(set_frame_value)(frame);
    stub("0x0143f9f0", 1);
    manager[0x38] = 1;
    if (unsigned char *dispatcher = get<unsigned char *>(manager, 0x40)) {
        void *method = get<void **>(dispatcher, 0)[6];
        reinterpret_cast<Dispatch *>(method)(dispatcher, get<void *>(manager, 0x50), get<void *>(manager, 0x48), groups, frame);
        if (rec.on()) {
            rec.pointer(dispatcher, 0);
            if (!rt::in_image(get<void *>(dispatcher, 0))) rec.pointer(get<void *>(dispatcher, 0), 0x30);
            rec.stub("{\"address\": \"" + rt::guest_hex(rt::guest_of(method)) + "\", \"argc\": 5}");
        }
    }
    manager[0x38] = 0;
    rt::fn<Void1>(queue_flush)(get<void *>(manager, 0x18));
    stub("0x0143e490", 1);
    if (void *workers = get<void *>(manager, 0x28)) {
        rt::fn<Void1>(worker_end)(workers);
        stub("0x01440160", 1);
    }
    rt::fn<Begin>(frame_end)(get<void *>(manager, 0x30), 0);
    finish();
}

extern "C" void bb_frame_timing_task_flush_queue_0143e490(unsigned char *queue)
{
    rt::Recorder rec;
    if (rec.begin("frame_timing_task_flush_queue_0143e490")) rec.pointer(queue, 0x10);
    auto method_stub = [&](const void *object, unsigned slot, int argc, const std::string &extra = "") {
        if (!rec.on()) return;
        rec.pointer(object, 0);
        const void *vtable = get<void *>(object, 0);
        if (!rt::in_image(vtable)) rec.pointer(vtable, slot);
        rec.stub("{\"address\": \"" + rt::guest_hex(rt::guest_of(get<void **>(object, 0)[slot / 8])) + "\", \"argc\": " +
                 std::to_string(argc) + extra + "}");
    };
    auto finish = [&] { rec.write("0x0143e490", "\"rdi\": \"" + (rec.on() ? "buf:" + rec.name(queue) : std::string()) + "\""); };
    if (!get<void *>(queue, 0x10)) return finish();
    unsigned char *lock = get<unsigned char *>(queue, 0x8);
    if (rec.on()) rec.pointer(queue, 0x8);
    reinterpret_cast<void (*)(void *, uint32_t)>(get<void **>(lock, 0)[3])(lock, 0xffffffffu);
    method_stub(lock, 0x18, 2);
    unsigned char *list = get<unsigned char *>(queue, 0x10);
    if (rec.on()) {
        rec.pointer(list, 0x8);
        rec.pointer(list, 0x10);
    }
    unsigned char **it = get<unsigned char **>(list, 0x8);
    if (it != get<unsigned char **>(list, 0x10)) {
        do {
            unsigned char *object = *it;
            if (rec.on()) rec.pointer(get<void *>(list, 0x8), static_cast<unsigned>(reinterpret_cast<unsigned char *>(it) -
                                                                                     get<unsigned char *>(list, 0x8)));
            void *run = get<void **>(object, 0)[2];
            reinterpret_cast<void (*)(void *)>(run)(object);
            method_stub(object, 0x10, 1);
            if (object) {
                unsigned char *allocator = rt::fn<unsigned char *(void *)>(allocator_of)(object);
                if (rec.on()) rec.stub("{\"address\": \"0x0247b720\", \"argc\": 1, \"ret\": \"" + rec.value(allocator) + "\"}");
                method_stub(object, 0x0, 1);
                reinterpret_cast<void (*)(void *)>(get<void **>(object, 0)[0])(object);
                method_stub(allocator, 0x70, 2);
                reinterpret_cast<void (*)(void *, void *)>(get<void **>(allocator, 0)[14])(allocator, object);
            }
            ++it;
            list = get<unsigned char *>(queue, 0x10);
        } while (it != get<unsigned char **>(list, 0x10));
        it = get<unsigned char **>(list, 0x8);
    }
    std::memcpy(list + 0x10, &it, sizeof it);
    method_stub(lock, 0x28, 1);
    reinterpret_cast<void (*)(void *)>(get<void **>(lock, 0)[5])(lock);
    finish();
}

extern "C" void bb_frame_timing_task_workers_step_014400a0(unsigned char *workers)
{
    rt::Recorder rec;
    if (rec.begin("frame_timing_task_workers_step_014400a0"))
        for (unsigned off : {0x48u, 0x50u, 0x58u, 0x60u, 0x68u}) rec.bytes(workers, off, 8);
    for (unsigned queue : {0x58u, 0x68u, 0x60u}) {
        rt::fn<Void2>(workers_select)(get<void *>(workers, 0x50), get<void *>(workers, queue));
        rt::fn<void(void *, int, void *)>(workers_run)(get<void *>(workers, 0x48), 0, get<void *>(workers, 0x50));
        rt::fn<Void1>(workers_a)(get<void *>(workers, 0x48));
        rt::fn<Void1>(workers_b)(get<void *>(workers, 0x48));
        rt::fn<Void1>(workers_c)(get<void *>(workers, 0x48));
        if (rec.on())
            rec.stub("{\"address\": \"0x0143d7c0\", \"argc\": 2}, {\"address\": \"0x0143e030\", \"argc\": 3}, "
                     "{\"address\": \"0x0143de20\", \"argc\": 1}, {\"address\": \"0x0143deb0\", \"argc\": 1}, "
                     "{\"address\": \"0x0143df10\", \"argc\": 1}");
    }
    rec.write("0x014400a0", "\"rdi\": \"" + (rec.on() ? "buf:" + rec.name(workers) : std::string()) + "\"");
}

extern "C" void bb_frame_timing_task_dispatch_0138a370(unsigned char *self, void *worker, void *tasks, uint32_t groups)
{
    rt::Recorder rec;
    if (rec.begin("frame_timing_task_dispatch_0138a370")) {
        rec.pointer(self, 0x48);
        rec.pointer(self, 0x50);
        rec.bytes(self, 0x68, 8);
        rec.bytes(self, 0x70, 1);
    }
    unsigned char *record = get<unsigned char *>(self, 0x48);
    if (rec.on()) rec.bytes(record, 0x18, 4);
    if (get<int32_t>(record, 0x18) < 0) {
        const int32_t count = rt::fn<int32_t(void *)>(entry_count)(self);
        if (rec.on()) rec.stub("{\"address\": \"0x01389df0\", \"argc\": 1, \"ret\": " + std::to_string(count) + "}");
        for (int32_t i = 0; count > 0 && i != count; i++) {
            unsigned char *entry = rt::fn<unsigned char *(void *, int32_t)>(entry_at)(self, i);
            const uint32_t id = get<uint32_t>(entry, 0);
            void *task = rt::fn<void *(void *, uint32_t)>(task_lookup)(tasks, id);
            if (rec.on()) {
                rec.bytes(entry, 0, 4);
                rec.stub("{\"address\": \"0x01389e10\", \"argc\": 2, \"ret\": \"" + rec.value(entry) + "\"}");
                rec.stub("{\"address\": \"0x0143f720\", \"argc\": 2, \"ret\": \"" + hex64(task) + "\"}");
            }
            if (task) {
                rt::fn<Void1>(task_wake)(task);
                if (rec.on()) {
                    rec.stub("{\"address\": \"0x0143ec40\", \"argc\": 1}");
                    rec.bytes(entry, 0x84, 1);
                }
                if (entry[0x84]) {
                    rt::fn<Void1>(task_wake_flagged)(task);
                    if (rec.on()) rec.stub("{\"address\": \"0x0143ed00\", \"argc\": 1}");
                }
            }
        }
        record = get<unsigned char *>(self, 0x48);
    }
    record[0x60] = self[0x70];
    rt::fn<Void1>(record_update)(record);
    if (rec.on()) rec.stub("{\"address\": \"0x0143c360\", \"argc\": 1}");
    unsigned char *job = get<unsigned char *>(self, 0x50);
    std::memcpy(job + 0x10, self + 0x48, 8);
    job = get<unsigned char *>(self, 0x50);
    std::memcpy(job + 0x8, &tasks, 8);
    job = get<unsigned char *>(self, 0x50);
    uint32_t mask[4] = {};
    rt::fn<void(void *, uint32_t)>(group_mask)(mask, groups);
    if (rec.on())
        rec.stub("{\"address\": \"0x0143bef0\", \"argc\": 2, \"writes\": [{\"arg\": 0, \"bytes\": \"" + rt::hex_bytes(mask, 4) + "\"}]}");
    std::memcpy(job + 0x18, mask, 4);
    rt::fn<Void2>(workers_select)(get<void *>(self, 0x68), get<void *>(self, 0x50));
    rt::fn<void(void *, int, void *)>(workers_run)(worker, 0, get<void *>(self, 0x68));
    rt::fn<Void1>(workers_a)(worker);
    rt::fn<Void1>(workers_b)(worker);
    rt::fn<Void1>(workers_c)(worker);
    rt::fn<Void1>(record_close)(get<void *>(self, 0x48));
    if (rec.on()) {
        rec.stub("{\"address\": \"0x0143d7c0\", \"argc\": 2}, {\"address\": \"0x0143e030\", \"argc\": 3}, "
                 "{\"address\": \"0x0143de20\", \"argc\": 1}, {\"address\": \"0x0143deb0\", \"argc\": 1}, "
                 "{\"address\": \"0x0143df10\", \"argc\": 1}, {\"address\": \"0x0143c450\", \"argc\": 1}");
        rec.name(job, 0x20);
        rec.write("0x0138a370", "\"rdi\": \"buf:" + rec.name(self) + "\", \"rsi\": \"" + hex64(worker) + "\", \"rdx\": \"" +
                                    hex64(tasks) + "\", \"rcx\": \"" + std::to_string(groups) + "\"");
    }
}
