// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the per-frame step of the main loop.
//
// frame_timing_frame_step (0x02418d20): asks the SprjWindow whether the game is running,
// creates the SprjFlipper on first use (allocator singleton, frame_timing_flipper_init), runs
// the frame limiter, runs the task update with a small 1/30 s descriptor, and returns
// "running and no quit requested". The singleton checks call the game's fatal error (the FD4
// singleton header's assertion) exactly where the original does.
//
// With BB_TARGET_FPS set, the task update's frame time is the one the community frame-rate
// patches compute here: 1/60 s (1/30 s for 30) unless the flipper's interval is not 1/30 s, then the measured frame time
// (+0x264, written by the limiter) clamped to [interval, 1/30 s] (maxss then minss, so NaN goes
// the same way). The patches also drop the SprjTask check; we keep it.
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/capture.h"
#include "runtime/guest.h"
#include "target_fps.h"

namespace {

constexpr uint32_t window_singleton = 0x05940500;    // SprjWindow*
constexpr uint32_t flipper_singleton = 0x059404f8;   // SprjFlipper*
constexpr uint32_t allocator_singleton = 0x05940408;
constexpr uint32_t task_singleton = 0x05940510;      // SprjTask*
constexpr uint32_t quit_gate_singleton = 0x05a9fa30;
constexpr uint32_t quit_query_singleton = 0x05aa54f8;
constexpr uint32_t quit_query_create = 0x024e2550;
constexpr uint32_t flipper_init = 0x02434520;
constexpr uint32_t pace_frame = 0x02434770;
constexpr uint32_t task_update = 0x024512a0;
constexpr uint32_t fatal_singleton_missing = 0x024b55b0;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd,
                   str_sprj_window = 0x04d3b00c, str_sprj_flipper = 0x04d3acac, str_sprj_task = 0x04d3b288;
constexpr uint32_t task_descriptor_vtable = 0x056efd30;
constexpr uint64_t flipper_size = 0x2c8;

using Fatal = void(const char *, int, const char *, const char *, ...);
using Bool0 = uint8_t(void *self);
using Alloc = void *(void *self, uint64_t size, uint64_t align);
using Object0 = void *();
using Init = void(void *flipper);
using TaskUpdate = void(void *unused, void *descriptor);

struct TaskDescriptor {
    void *vtable;
    uint32_t interval_bits;
};

// The engine's fatal error does not return in the game; the code after each call continues as
// the original's does (QUIRKS.md: Ghidra hides those jumps).
void fatal(uint32_t singleton_name)
{
    rt::fn<Fatal>(fatal_singleton_missing)(rt::ptr<const char>(str_singleton_header), 0xb1,
                                           rt::ptr<const char>(str_singleton_func),
                                           rt::ptr<const char>(singleton_name));
}

void *virtual_fn(void *object, unsigned offset)
{
    return (*static_cast<void ***>(object))[offset / 8];
}

uint32_t guest_address(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

// Recording for tools/verify.py: objects become small buffers whose vtable slots point at the
// real targets (as PS4 addresses), which the case stubs with the values this run returned.
struct Capture {
    int number = -1;
    std::string buffers, memory, stubs;

    void buffer(const char *name, unsigned size)
    {
        buffers += std::string(buffers.empty() ? "" : ", ") + "\"" + name + "\": {\"size\": " + std::to_string(size) + "}";
    }
    void mem(const std::string &entry) { memory += std::string(memory.empty() ? "" : ", ") + entry; }
    void stub(const std::string &entry) { stubs += std::string(stubs.empty() ? "" : ", ") + entry; }
    static std::string hex32(uint32_t v)
    {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08x", v);
        return b;
    }
    // An object with one virtual slot used: OBJ -> OBJ_vt, OBJ_vt+offset -> target.
    void object(const char *name, uint32_t singleton, unsigned offset, uint32_t target)
    {
        const std::string vt = std::string(name) + "_vt";
        buffer(name, 8);
        buffer(vt.c_str(), offset + 8);
        mem("{\"addr\": \"" + hex32(singleton) + "\", \"pointer\": \"" + name + "\"}");
        mem("{\"addr\": \"buf:" + std::string(name) + "\", \"pointer\": \"" + vt + "\"}");
        mem("{\"addr\": \"buf:" + vt + "+" + std::to_string(offset) + "\", \"guest\": \"" + hex32(target) + "\"}");
    }
    void null(uint32_t singleton) { mem("{\"addr\": \"" + hex32(singleton) + "\", \"bytes\": \"0000000000000000\"}"); }
};

}  // namespace

extern "C" uint8_t bb_frame_timing_frame_step()
{
    Capture cap;
    cap.number = rt_capture_begin("frame_timing_frame_step");

    void *window = *rt::ptr<void *>(window_singleton);
    if (!window)
        fatal(str_sprj_window);
    void *window_running = virtual_fn(window, 0x18);
    const uint8_t running = reinterpret_cast<Bool0 *>(window_running)(window);
    if (cap.number >= 0) {
        cap.object("window", window_singleton, 0x18, guest_address(window_running));
        cap.stub("{\"address\": \"" + Capture::hex32(guest_address(window_running)) + "\", \"argc\": 1, \"ret\": " + std::to_string(running) + "}");
        cap.buffer("flipper", flipper_size);
    }

    unsigned char *flipper = *rt::ptr<unsigned char *>(flipper_singleton);
    if (!flipper) {
        void *allocator = *rt::ptr<void *>(allocator_singleton);
        void *alloc = virtual_fn(allocator, 0x58);
        flipper = static_cast<unsigned char *>(reinterpret_cast<Alloc *>(alloc)(allocator, flipper_size, 8));
        if (cap.number >= 0) {
            cap.null(flipper_singleton);
            cap.object("allocator", allocator_singleton, 0x58, guest_address(alloc));
            cap.stub("{\"address\": \"" + Capture::hex32(guest_address(alloc)) + "\", \"argc\": 3, \"ret\": \"buf:flipper\"}");
            cap.stub("{\"address\": \"0x02434520\", \"argc\": 1}");
        }
        if (flipper) {
            rt::fn<Init>(flipper_init)(flipper);
            *rt::ptr<unsigned char *>(flipper_singleton) = flipper;
        } else {
            *rt::ptr<unsigned char *>(flipper_singleton) = nullptr;
            fatal(str_sprj_flipper);
        }
        flipper[0x272] = 1;
    } else if (cap.number >= 0) {
        cap.mem("{\"addr\": \"" + Capture::hex32(flipper_singleton) + "\", \"pointer\": \"flipper\"}");
    }
    rt::fn<Init>(pace_frame)(flipper);
    if (cap.number >= 0)
        cap.stub("{\"address\": \"0x02434770\", \"argc\": 1}");

    using frame_timing::k_interval_30;
    TaskDescriptor descriptor{rt::ptr<unsigned char>(task_descriptor_vtable) + 0x10, k_interval_30};
    const frame_timing::Target target = frame_timing::target_fps();
    if (target != frame_timing::Target::original) {
        // The 60 and uncapped patches also start the descriptor at 1/60 s (kept when the interval
        // is exactly 1/30 s); the 30 FPS patch leaves it at 1/30 s.
        if (target != frame_timing::Target::fps30)
            descriptor.interval_bits = frame_timing::k_interval_60;
        uint32_t interval, measured;
        std::memcpy(&interval, flipper + 0x18, 4);
        std::memcpy(&measured, flipper + 0x264, 4);
        if (interval != k_interval_30) {
            __m128 dt = _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(measured)));
            dt = _mm_max_ss(dt, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(interval))));
            dt = _mm_min_ss(dt, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(k_interval_30))));
            descriptor.interval_bits = static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(dt)));
        }
    }
    if (cap.number >= 0) {
        // The flipper's interval and measured frame time after the limiter: read only with
        // BB_TARGET_FPS, recorded always so the option can be checked on real frames.
        char hexbuf[2][9];
        for (int i = 0; i < 2; i++) {
            const unsigned char *p = flipper + (i ? 0x264 : 0x18);
            std::snprintf(hexbuf[i], sizeof hexbuf[i], "%02x%02x%02x%02x", p[0], p[1], p[2], p[3]);
        }
        cap.mem(std::string("{\"addr\": \"buf:flipper+24\", \"bytes\": \"") + hexbuf[0] + "\"}");
        cap.mem(std::string("{\"addr\": \"buf:flipper+612\", \"bytes\": \"") + hexbuf[1] + "\"}");
    }
    if (!*rt::ptr<void *>(task_singleton))
        fatal(str_sprj_task);
    // The original leaves the first argument register undefined: task_update does not read it.
    rt::fn<TaskUpdate>(task_update)(nullptr, &descriptor);
    if (cap.number >= 0) {
        cap.buffer("task", 8);
        cap.mem("{\"addr\": \"" + Capture::hex32(task_singleton) + "\", \"pointer\": \"task\"}");
        cap.stub("{\"address\": \"0x024512a0\", \"argi\": [1], \"argmem\": [{\"arg\": 1, \"size\": 12}]}");
    }

    void *gate = *rt::ptr<void *>(quit_gate_singleton);
    bool need_window = true;
    if (gate) {
        void *gate_fn = virtual_fn(gate, 0x28);
        const uint8_t open = reinterpret_cast<Bool0 *>(gate_fn)(gate);
        need_window = open != 0;
        if (cap.number >= 0) {
            cap.object("gate", quit_gate_singleton, 0x28, guest_address(gate_fn));
            cap.stub("{\"address\": \"" + Capture::hex32(guest_address(gate_fn)) + "\", \"argc\": 1, \"ret\": " + std::to_string(open) + "}");
        }
    } else if (cap.number >= 0) {
        cap.null(quit_gate_singleton);
    }
    if (need_window && !*rt::ptr<void *>(window_singleton))
        fatal(str_sprj_window);

    void *query = *rt::ptr<void *>(quit_query_singleton);
    const bool created = query == nullptr;
    if (created)
        query = rt::fn<Object0>(quit_query_create)();
    void *query_fn = virtual_fn(query, 0x98);
    const uint8_t quitting = reinterpret_cast<Bool0 *>(query_fn)(query);
    if (cap.number >= 0) {
        if (created) {
            cap.null(quit_query_singleton);
            cap.buffer("query", 8);
            cap.buffer("query_vt", 0x98 + 8);
            cap.mem("{\"addr\": \"buf:query\", \"pointer\": \"query_vt\"}");
            cap.mem("{\"addr\": \"buf:query_vt+152\", \"guest\": \"" + Capture::hex32(guest_address(query_fn)) + "\"}");
            cap.stub("{\"address\": \"0x024e2550\", \"ret\": \"buf:query\"}");
        } else {
            cap.object("query", quit_query_singleton, 0x98, guest_address(query_fn));
        }
        cap.stub("{\"address\": \"" + Capture::hex32(guest_address(query_fn)) + "\", \"argc\": 1, \"ret\": " + std::to_string(quitting) + "}");
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x02418d20\", \"id\": \"capture_%04d\", \"returns\": \"i8\", \"args\": {}, ",
                      cap.number);
        const std::string json = std::string(head) + "\"buffers\": {" + cap.buffers + "}, \"memory\": [" + cap.memory +
                                 "], \"stubs\": [" + cap.stubs + "], \"imports\": []}";
        rt_capture_write("frame_timing_frame_step", cap.number, json.c_str());
    }
    return running & (quitting ^ 1);
}
