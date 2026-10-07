// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the task manager's frame (FD4::FD4TaskManager, singleton 0x058b2e30).
//
// The frame step (0x02418d20) runs the frame's tasks through 0x024512a0 -> 0x01388c60 ->
// 0x01388c70, which brackets the dispatch with setup and teardown.
// - frame_timing_task_run_all_01388c60 (0x01388c60): runs the tasks of every group (-1).
// - frame_timing_task_set_frame_value_0143f9f0 (0x0143f9f0): copies the float at +0x8 of the
//   frame's information to the global 0x058b7e08.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "runtime/guest.h"
#include "runtime/recorder.hpp"

namespace {

constexpr uint32_t task_run = 0x01388c70;
constexpr uint32_t frame_value = 0x058b7e08;

using TaskRun = void(void *manager, uint32_t groups, void *frame);

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
