// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the frame limiter (SprjFlipper's per-frame wait).
//
// frame_timing_pace_frame (0x02434770) runs once per frame from frame_timing_frame_step. It
// applies the flip mode (a pending one at +0xc if +0x276 is set, else the current one at
// +0x8; read from the Game.FlipMode config by frame_timing_flipper_init), waits until the
// frame's time budget is used, records the frame in a 32-entry ring and a 16-entry history,
// and derives the frame rate. In this build 0x013e3980 always returns 1, so only the spinning
// wait runs; the sleeping wait is kept for fidelity.
//
// BB_TARGET_FPS (our option, not in the original; target_fps.h): unset keeps the original
// behaviour exactly. 30, 60 and uncapped do what the community patches "30 FPS++", "60 FPS++" and
// "Uncap FPS++" do to this function (checked against them: verify.py run --patch ... --env ...):
// a fixed mode (interval 1/30, 1/60 or 1/240 s), no overrides, and the sync interval kept on late
// frames. The frame step and the flipper constructor apply the rest of those patches.
//
// BB_LIMITER_WAIT=sleep (our option): use the sleeping wait the game carries but never selects,
// instead of the spinning one: it sleeps until about 5 ms before the frame's end and spins only
// the rest, so a fast machine does not keep a core busy. Only the wait changes; unset keeps the
// original's spinning wait.
//
// Fidelity: every conversion and comparison mirrors the original instruction (unsigned 64-bit
// to float through the halving pattern, truncating double to unsigned 64-bit through the 2^63
// adjustment, vminss operand order, NaN behaviour of each branch). Build with
// -ffp-contract=off (game/CMakeLists.txt).
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/capture.h"
#include "runtime/guest.h"
#include "target_fps.h"

namespace {

// Game functions and library thunks.
constexpr uint32_t window_singleton = 0x05940500;    // SprjWindow*
constexpr uint32_t notify_singleton = 0x05940e00;    // object told about a 60 FPS target
constexpr uint32_t fatal_singleton_missing = 0x024b55b0;
constexpr uint32_t limiter_mode_flag = 0x013e3980;   // returns 1 in this build
constexpr uint32_t gettimeofday_thunk = 0x02fbe728;
constexpr uint32_t usleep_thunk = 0x02fbfe68;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd,
                   str_sprj_window = 0x04d3b00c;

// Constants of the original (.rodata).
constexpr float k_us_per_s = 1000000.0f;
constexpr float k_neg_us_per_s = -1000000.0f;
constexpr float k_min_wait = 0.001f, k_sleep_threshold = 0.005f, k_sleep_margin = -0.005f;
constexpr float k_thousand = 1000.0f, k_late_fraction = 0.01f, k_history = 16.0f;
constexpr double k_us_per_s_d = 1000000.0, k_two_pow_63 = 9223372036854775808.0;
constexpr uint32_t k_interval_30 = 0x3d088889;   // 1/30 s as float bits
constexpr uint32_t k_interval_240 = 0x3b888889;  // 1/240 s: the Uncap FPS++ patch's interval
constexpr uint32_t k_interval_60 = 0x3c888889;   // 1/60 s as float bits

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

using Gettimeofday = int(Timeval *tv, void *tz);
using Usleep = int(unsigned microseconds);
using ModeFlag = uint8_t();
using Fatal = void(const char *, int, const char *, const char *, ...);

// Flipper field access by offset.
template <typename T> T get(unsigned char *f, unsigned off) { T v; std::memcpy(&v, f + off, sizeof v); return v; }
template <typename T> void put(unsigned char *f, unsigned off, T v) { std::memcpy(f + off, &v, sizeof v); }

// vcvtsi2ss on an unsigned value: halve with the low bit kept, convert, double.
float u64_to_f32(uint64_t x)
{
    if (static_cast<int64_t>(x) >= 0)
        return _mm_cvtss_f32(_mm_cvtsi64_ss(_mm_setzero_ps(), static_cast<int64_t>(x)));
    const uint64_t half = (x >> 1) | (x & 1);
    const float f = _mm_cvtss_f32(_mm_cvtsi64_ss(_mm_setzero_ps(), static_cast<int64_t>(half)));
    return f + f;
}

// vcvttsd2si to unsigned: subtract 2^63 and flip the top bit when the value is >= 2^63.
uint64_t f64_to_u64(double d)
{
    if (d >= k_two_pow_63)
        return static_cast<uint64_t>(_mm_cvttsd_si64(_mm_set_sd(d - k_two_pow_63))) ^ 0x8000000000000000ull;
    return static_cast<uint64_t>(_mm_cvttsd_si64(_mm_set_sd(d)));
}

int32_t f32_to_i32(float f) { return _mm_cvttss_si32(_mm_set_ss(f)); }

// vminss xmm0, xmm3, xmm0: the first operand when it is less, else the second.
float min_first(float a, float b) { return a < b ? a : b; }

// Recording of one call's inputs for tools/verify.py (BB_CAPTURE_DIR). Runs of clock reads
// (the spinning wait polls the clock thousands of times) are kept as one series entry.
struct Capture {
    int number = -1;
    std::string imports, stubs;
    std::string series;            // pending run of gettimeofday results, microseconds
    int series_ret = 0;
    unsigned calls = 0;
    static constexpr unsigned max_calls = 500000;

    bool on() const { return number >= 0 && calls <= max_calls; }

    static std::string hex(const void *p, size_t n)
    {
        static const char digits[] = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; i++) {
            const unsigned b = static_cast<const unsigned char *>(p)[i];
            s += digits[b >> 4];
            s += digits[b & 15];
        }
        return s;
    }
    void add(const std::string &entry)
    {
        if (!imports.empty()) imports += ", ";
        imports += entry;
    }
    void flush()
    {
        if (series.empty()) return;
        add("{\"name\": \"gettimeofday\", \"argc\": 2, \"ret\": " + std::to_string(series_ret) +
            ", \"series\": {\"arg\": 0, \"format\": \"timeval_us\", \"values\": [" + series + "]}}");
        series.clear();
    }
    void gettimeofday(int ret, const Timeval &tv)
    {
        if (!on() || ++calls > max_calls) return;
        if (!series.empty() && ret != series_ret) flush();
        series_ret = ret;
        if (!series.empty()) series += ",";
        series += std::to_string(tv.seconds * 1000000 + tv.microseconds);
    }
    void usleep(int ret)
    {
        if (!on() || ++calls > max_calls) return;
        flush();
        add("{\"name\": \"sceKernelUsleep\", \"argc\": 1, \"ret\": " + std::to_string(ret) + "}");
    }
    void mode_flag(uint8_t ret)
    {
        if (!on()) return;
        stubs = "{\"address\": \"0x013e3980\", \"ret\": " + std::to_string(ret) + "}";
    }
};

using frame_timing::Target;
using frame_timing::target_fps;

bool sleeping_wait()
{
    static const bool sleep = [] {
        const char *v = std::getenv("BB_LIMITER_WAIT");
        if (!v || !*v || !std::strcmp(v, "spin")) return false;
        if (!std::strcmp(v, "sleep")) return true;
        std::fprintf(stderr, "frame limiter: BB_LIMITER_WAIT=%s is not sleep or spin; ignored\n", v);
        return false;
    }();
    return sleep;
}

Timeval now_tv(Capture &cap)
{
    Timeval tv;
    const int r = rt::fn<Gettimeofday>(gettimeofday_thunk)(&tv, nullptr);
    cap.gettimeofday(r, tv);
    return tv;
}

uint64_t to_us(const Timeval &tv)
{
    return static_cast<uint64_t>(tv.seconds) * 1000000ull + static_cast<uint64_t>(tv.microseconds);
}

}  // namespace

extern "C" void bb_frame_timing_pace_frame(unsigned char *f)
{
    Capture cap;
    std::string flipper_before;
    void *notify_before = nullptr;
    cap.number = rt_capture_begin("frame_timing_pace_frame");
    if (cap.number >= 0) {
        flipper_before = Capture::hex(f, 0x2c8);
        notify_before = *rt::ptr<void *>(notify_singleton);
    }

    if (*rt::ptr<void *>(window_singleton) == nullptr)
        rt::fn<Fatal>(fatal_singleton_missing)(rt::ptr<const char>(str_singleton_header), 0xb1,
                                               rt::ptr<const char>(str_singleton_func),
                                               rt::ptr<const char>(str_sprj_window));
    const uint8_t spin_only = rt::fn<ModeFlag>(limiter_mode_flag)();
    cap.mode_flag(spin_only);

    // Flip mode: pending (+0xc) when +0x276 is set, else current (+0x8).
    const uint8_t pending = f[0x276];
    f[0x275] = pending;
    f[0x276] = 0;
    const uint32_t mode = get<uint32_t>(f, pending ? 0xc : 0x8);
    const Target target = target_fps();
    uint32_t reset = 0;
    if (target != Target::original) {
        // Our option, as the community patches rewrite this part: the mode switch always takes
        // one entry (mode 2 for 30 and 60, while the real mode is in range; mode 0 for uncapped,
        // always) with patched values and a zero vblank word, and every override below is
        // skipped (the reset flag is neither honoured nor cleared).
        if (target == Target::uncapped) {
            put<uint32_t>(f, 0x10, 1); f[0x14] = 0; put<uint32_t>(f, 0x18, k_interval_240); put<uint64_t>(f, 0x268, 0);
        } else if (mode <= 4) {
            put<uint32_t>(f, 0x10, 1); f[0x14] = 1;
            put<uint32_t>(f, 0x18, target == Target::fps30 ? k_interval_30 : k_interval_60);
            put<uint64_t>(f, 0x268, 0);
        }
    } else switch (mode) {
    case 0: put<uint32_t>(f, 0x10, 2); f[0x14] = 1; put<uint32_t>(f, 0x18, k_interval_30); put<uint64_t>(f, 0x268, 0x1e00000001ull); break;
    case 1: put<uint32_t>(f, 0x10, 2); f[0x14] = 1; put<uint32_t>(f, 0x18, k_interval_30); put<uint64_t>(f, 0x268, 0x1e00000000ull); break;
    case 2: put<uint32_t>(f, 0x10, 1); f[0x14] = 1; put<uint32_t>(f, 0x18, k_interval_60); put<uint64_t>(f, 0x268, 0x1e00000000ull); break;
    case 3: put<uint32_t>(f, 0x10, 1); f[0x14] = 0; put<uint32_t>(f, 0x18, k_interval_30); put<uint64_t>(f, 0x268, 0x1e00000001ull); break;
    case 4: put<uint32_t>(f, 0x10, 1); f[0x14] = 0; put<uint32_t>(f, 0x18, k_interval_30); put<uint64_t>(f, 0x268, 0x1e00000000ull); break;
    default: break;
    }

    if (target == Target::original && f[0x2c4]) {
        put<uint32_t>(f, 0x10, 1); f[0x14] = 0; put<uint32_t>(f, 0x18, k_interval_30);
        put<uint32_t>(f, 0x268, 0); put<uint32_t>(f, 0x26c, 0); f[0x2c4] = 0;
        reset = 1;
    }
    if (target == Target::original) {
        if (get<int32_t>(f, 0x2bc) >= 0) put<uint32_t>(f, 0x268, get<uint32_t>(f, 0x2bc));
        if (get<int32_t>(f, 0x2c0) >= 0) put<uint32_t>(f, 0x26c, get<uint32_t>(f, 0x2c0));
        if (f[0x271]) { put<uint32_t>(f, 0x268, 0); put<uint32_t>(f, 0x26c, 0); f[0x271] = 0; }
        if (f[0x272]) put<uint64_t>(f, 0x268, 0);
    }

    // Frame start: the previous frame's end time becomes this frame's start.
    put<uint64_t>(f, 0x20, get<uint64_t>(f, 0x28));
    uint64_t now = to_us(now_tv(cap));
    put<uint64_t>(f, 0x28, now);

    uint64_t budget_us = f64_to_u64(static_cast<double>(get<float>(f, 0x18)) * k_us_per_s_d);

    // Sum of recent frame times while they were marked late (+0x68), up to the window length.
    const uint32_t window = get<uint32_t>(f, spin_only ? 0x268 : 0x26c);
    uint32_t counted = 0;
    uint64_t late_sum = 0;
    for (uint32_t back = 0; counted < window; back--) {
        const unsigned slot = ((get<uint32_t>(f, 0x260) + back) & 0x1f) << 4;
        late_sum += get<uint64_t>(f, 0x60 + slot);
        counted++;
        if (!f[0x68 + slot]) break;
    }
    // Catch-up: shorten the budget when recent frames ran over, never below a third.
    if (f[0x270]) {
        uint64_t limit = static_cast<uint64_t>(counted + 1) * budget_us;
        if (late_sum + budget_us > limit) {
            const uint64_t third = budget_us / 3;
            if (limit > late_sum) {
                limit -= late_sum;
                if (budget_us < limit) limit = budget_us;
                if (third > limit) limit = third;
                budget_us = limit;
            } else {
                budget_us = third;
            }
        }
    }

    if (!(reset | f[0x273])) {
        const float budget_s = u64_to_f32(budget_us) / k_us_per_s;
        const float elapsed_s = u64_to_f32(now - get<uint64_t>(f, 0x20)) / k_us_per_s;
        float remaining = budget_s - elapsed_s;
        if (!(0.0f > remaining)) {
            for (;;) {
                const float wait = min_first(budget_s, remaining);
                if (!(wait > 0.0f)) break;
                if ((!spin_only || sleeping_wait()) && wait > k_min_wait && wait > k_sleep_threshold) {
                    const int32_t us = f32_to_i32(((wait + k_sleep_margin) * k_thousand) * k_thousand);
                    if (us > 0) cap.usleep(rt::fn<Usleep>(usleep_thunk)(static_cast<unsigned>(us)));
                }
                now = to_us(now_tv(cap));
                put<uint64_t>(f, 0x28, now);
                remaining = budget_s + u64_to_f32(now - get<uint64_t>(f, 0x20)) / k_neg_us_per_s;
                if (0.0f > remaining) break;
            }
        }
    }

    if (f[0x273]) f[0x270] = 1;
    const uint32_t slot_index = (get<uint32_t>(f, 0x260) + 1) & 0x1f;
    put<uint32_t>(f, 0x260, slot_index);
    put<uint64_t>(f, 0x60 + (slot_index << 4), now - get<uint64_t>(f, 0x20));
    const uint8_t was_late = f[0x270];
    f[0x68 + (slot_index << 4)] = was_late;

    const uint64_t frame_us = get<uint64_t>(f, 0x28) - get<uint64_t>(f, 0x20);
    const float frame_s = u64_to_f32(frame_us) / k_us_per_s;
    put<float>(f, 0x264, frame_s);
    uint8_t window_used = (window == 0);
    if (was_late) window_used = (counted >= window);

    const float interval = get<float>(f, 0x18);
    const uint64_t tolerance_us = f64_to_u64(static_cast<double>(interval * k_late_fraction) * k_us_per_s_d);
    const uint64_t interval_us = f64_to_u64(static_cast<double>(interval) * k_us_per_s_d);
    const uint64_t allowed = static_cast<uint64_t>(counted + 1) * interval_us + tolerance_us;
    uint8_t on_time = (frame_us + late_sum < allowed) ? 1 : window_used;
    if (f[0x273]) on_time = 0;
    f[0x270] = on_time ^ 1;
    // The patches keep the sync interval when the frame was late (the original zeroes it).
    put<uint32_t>(f, 0x10, on_time || target != Target::original ? get<uint32_t>(f, 0x10) : 0u);

    // 16-frame history of frame times and the frame rate it gives.
    float sum = get<float>(f, 0x27c);
    put<float>(f, 0x278, sum);
    sum = sum + 0.0f;
    for (unsigned off = 0x280; off <= 0x2b4; off += 4) {
        const float v = get<float>(f, off);
        put<float>(f, off - 4, v);
        sum = sum + v;
    }
    put<float>(f, 0x2b4, frame_s);
    const float total = sum + frame_s;
    if (total > k_min_wait) put<float>(f, 0x2b8, k_history / total);
    else put<uint32_t>(f, 0x2b8, 0);

    f[0x273] = 0;
    if (unsigned char *notify = *rt::ptr<unsigned char *>(notify_singleton); notify && f[0x274]) {
        notify[0x4c] = 1;
        notify[0x4d] = 1;
        put<uint32_t>(notify, 0x50, 60);
    }
    f[0x274] = 0;

    if (cap.number >= 0 && cap.on()) {
        cap.flush();
        char head[512];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x02434770\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:flipper\"}, \"buffers\": {\"flipper\": {\"size\": 712, \"bytes\": \"",
                      cap.number);
        std::string json = head + flipper_before + "\"}, \"window\": {\"size\": 8}";
        json += notify_before ? ", \"notify\": {\"size\": 96}}" : "}";
        json += ", \"memory\": [{\"addr\": \"0x05940500\", \"pointer\": \"window\"}, ";
        json += notify_before ? "{\"addr\": \"0x05940e00\", \"pointer\": \"notify\"}"
                              : "{\"addr\": \"0x05940e00\", \"bytes\": \"0000000000000000\"}";
        json += "], \"stubs\": [" + cap.stubs + "], \"imports\": [" + cap.imports + "]}";
        rt_capture_write("frame_timing_pace_frame", cap.number, json.c_str());
    }
}
