// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the SprjFlipper constructor.
//
// frame_timing_flipper_init (0x02434520) sets the vtable, reads the config value Game.FlipMode
// twice (the current mode, default 4; the pending mode, default 3; each clamped to 4), sets a
// 1/30 s interval with sync interval 0, stamps the clock into its eight time fields and clears
// the frame ring, the history and the flags. The limiter (frame_limiter.cpp) uses the fields.
// With BB_TARGET_FPS=60 the first interval is 1/60 s, as the 60 FPS patches write it.
#include <bit>
#include <cstdint>

#include "../engine/wide_string.h"
#include "flipper.h"
#include "runtime/original.h"
#include "target_fps.h"

namespace {

using namespace frame_timing;

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

struct Config;

RT_GLOBAL(0x0575a910, flipper_vtable, const uint8_t);
RT_GLOBAL(0x0593d710, config_instance, Config *);
RT_GLOBAL(0x04d7ea96, flip_mode_key, const char16_t);   // u"Game.FlipMode"
RT_ORIGINAL(0x024eba00, config_get_int, uint32_t(Config *, engine::WideString *key, uint32_t fallback));
RT_ORIGINAL(0x02fbe728, c_gettimeofday, int(Timeval *, void *timezone));

constexpr uint32_t default_flip_mode = 4, default_pending_flip_mode = 3, max_flip_mode = 4;
constexpr uint64_t us_per_second = 1000000;
constexpr int32_t no_override = -1;

uint32_t read_flip_mode(uint32_t fallback)
{
    engine::WideString key{};
    engine::wide_string_construct(&key, flip_mode_key.address());
    key.owns = 1;
    const uint32_t mode = config_get_int(config_instance.get(), &key, fallback);
    engine::wide_string_release(&key);
    return mode <= max_flip_mode ? mode : max_flip_mode;
}

}  // namespace

extern "C" void bb_frame_timing_flipper_init(SprjFlipper *f)
{
    f->vtable = flipper_vtable.address();
    f->flip_mode = read_flip_mode(default_flip_mode);
    f->pending_flip_mode = read_flip_mode(default_pending_flip_mode);
    f->sync_interval = 0;
    f->mode_flag_0x14 = 1;
    f->interval = std::bit_cast<float>(fixed_step_bits());

    Timeval now_tv;
    c_gettimeofday(&now_tv, nullptr);
    const uint64_t now = static_cast<uint64_t>(now_tv.seconds) * us_per_second + static_cast<uint64_t>(now_tv.microseconds);
    f->frame_start_us = now;
    f->frame_end_us = now;
    for (uint64_t &stamp : f->clock_stamps) stamp = now;

    f->frame_rate = 0.0f;
    f->has_pending_mode = 0;
    f->notify_60_fps = 0;
    f->used_pending_mode = 0;
    f->catching_up = 0;
    f->reset_late_windows = 0;
    f->no_late_windows = 0;
    f->skip_wait = 0;
    f->late_window_spin = 0;
    f->late_window_sleep = 0;
    f->ring_index = 0;
    f->frame_seconds = 0.0f;
    f->late_window_spin_override = no_override;
    f->late_window_sleep_override = no_override;
    f->reset_mode = 0;
    f->flag_0x2c5 = 0;
    for (FrameRecord &record : f->ring) {
        record.frame_us = 0;
        record.late = 0;
    }
    for (float &seconds : f->history) seconds = 0.0f;
}
