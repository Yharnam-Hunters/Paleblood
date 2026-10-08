// SPDX-License-Identifier: GPL-2.0-or-later
// Render (YEBIS post-processing middleware): recursive sample parameters for a blur.
//
// render_yebis_get_recursive_sample_parameters (0x00fbc3e0; GPUTexUtil_GetRecursiveSampleParameters
// in the middleware's own messages) splits a blur of `length` texels into recursive passes of
// `base_samples` taps each: how many passes, how many samples in the last one, and the scale of
// the first pass. `mode` 1 wants an odd base, mode 2 an even one of at least 4; `max_passes`
// (when positive) caps the passes. Every check reports through the C library's _Assert with the
// middleware's condition, as the original does; the code goes on afterwards.
//
// The community frame-rate patches (30, 60, 90 FPS++ and Uncap FPS++) all remove the first check
// (base_samples >= 3): with BB_TARGET_FPS set it is skipped here too.
#include <cstdint>

#include "../engine/scalar.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

// The executable's call stubs into the C library
RT_ORIGINAL(0x02fbf178, c_assert, void(const char *message, const char *function));
RT_ORIGINAL(0x02fbea78, c_flog, float(float x, int32_t base));   // base 0: natural logarithm
RT_ORIGINAL(0x02fbe948, c_powf, float(float x, float y));
constexpr int32_t natural_log = 0;

// The middleware's messages: the function, and one per failed condition
RT_GLOBAL(0x04c00f90, function_name, const char);
RT_GLOBAL(0x04c00f2c, message_base_at_least_3, const char);
RT_GLOBAL(0x04c00fb8, message_odd_base, const char);
RT_GLOBAL(0x04c0100f, message_odd_base_at_least_3, const char);
RT_GLOBAL(0x04c01073, message_even_base, const char);
RT_GLOBAL(0x04c010cf, message_even_base_at_least_4, const char);
RT_GLOBAL(0x04c01133, message_texel_at_least_1, const char);
RT_GLOBAL(0x04c0119f, message_final_samples_positive, const char);

// Float constants in the middleware's read-only data: 1.0, 1.1 and -1.0
RT_GLOBAL(0x04bffd70, one, const float);
RT_GLOBAL(0x04bffd74, pass_threshold, const float);
RT_GLOBAL(0x04bffd78, minus_one, const float);

constexpr int32_t mode_odd = 1, mode_even = 2;
constexpr int32_t min_base_samples = 3, min_even_base_samples = 4;

template <typename Message> void check_failed(const Message &message)
{
    c_assert(message.address(), function_name.address());
}

bool is_odd(int32_t n) { return (n & 1) != 0; }

}  // namespace

extern "C" void bb_render_yebis_get_recursive_sample_parameters(int32_t base_samples, int32_t mode, int32_t max_passes,
                                                               int32_t *out_passes, int32_t *out_final, float *out_scale,
                                                               float length, float texel)
{
    if (base_samples < min_base_samples && frame_timing::target_fps() == frame_timing::Target::original)
        check_failed(message_base_at_least_3);
    const float base = static_cast<float>(base_samples);
    if (mode == mode_odd) {
        if (!is_odd(base_samples)) check_failed(message_odd_base);
        if (base_samples < min_base_samples) check_failed(message_odd_base_at_least_3);
    } else if (mode == mode_even) {
        if (is_odd(base_samples)) check_failed(message_even_base);
        if (base_samples < min_even_base_samples) check_failed(message_even_base_at_least_4);
    }

    // The texel spacing, clamped down so that one pass does not overshoot the length.
    float spacing = texel;
    bool spacing_too_small = false;
    if (texel > one.get() && base * texel >= length) {
        spacing = length / base;
        if (!(spacing > one.get())) spacing_too_small = true;
    }
    float divisor;
    if (spacing_too_small) {
        divisor = one.get();
    } else {
        if (!(spacing >= one.get())) check_failed(message_texel_at_least_1);
        divisor = spacing;
    }

    const float ratio = length / divisor + one.get();
    int32_t passes = 0, final_samples = 0;
    float scale = 0.0f;
    if (ratio > pass_threshold.get()) {
        const float log_ratio = c_flog(ratio, natural_log);
        const float log_base = c_flog(base, natural_log);
        passes = engine::truncate(engine::round_up(engine::max_of(log_ratio / log_base, one.get())));
        const float total = base * ratio;
        const float power = c_powf(base, static_cast<float>(passes));
        int32_t last = engine::truncate(engine::round_up(total / power));
        if (last <= 0) check_failed(message_final_samples_positive);
        if (last == 0) last = base_samples;
        // The last pass is rounded up to the mode's parity.
        final_samples = last;
        if (mode == mode_odd && !is_odd(last)) final_samples = last + 1;
        if (mode == mode_even && is_odd(last)) final_samples = last + 1;
        if (final_samples > base_samples) final_samples = base_samples;
        if (max_passes > 0 && passes > max_passes) {
            passes = max_passes;
            final_samples = base_samples;
        }
        const float power_less = c_powf(base, static_cast<float>(passes - 1));
        scale = length / (power_less * static_cast<float>(final_samples) + minus_one.get());
    }

    if (out_passes) *out_passes = passes;
    if (out_final) *out_final = final_samples;
    if (out_scale) *out_scale = scale;
}
