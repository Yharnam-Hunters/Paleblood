// SPDX-License-Identifier: GPL-2.0-or-later
// Render (YEBIS post-processing middleware): recursive sample parameters for a blur.
//
// render_yebis_get_recursive_sample_parameters (0x00fbc3e0; GPUTexUtil_GetRecursiveSampleParameters
// in the middleware's own messages) splits a blur of `length` texels into recursive passes of
// `base_samples` taps each: how many passes, how many samples in the last one, and the scale of
// the first pass. `mode` 1 wants an odd base, mode 2 an even one of at least 4; `max_passes`
// (when positive) caps the passes. Every check reports through the C library's _Assert with the
// middleware's file, line and condition, as the original does; the code goes on afterwards.
//
// The community frame-rate patches (30, 60, 90 FPS++ and Uncap FPS++) all remove the first check
// (base_samples >= 3): with BB_TARGET_FPS set it is skipped here too.
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t assert_thunk = 0x02fbf178;   // _Assert(message, function)
constexpr uint32_t flog_thunk = 0x02fbea78;     // _FLog(x, 0): natural logarithm
constexpr uint32_t powf_thunk = 0x02fbe948;
constexpr uint32_t str_function = 0x04c00f90;   // "GPUTexUtil_GetRecursiveSampleParameters"
constexpr uint32_t msg_base3 = 0x04c00f2c, msg_mod = 0x04c00fb8, msg_base3_mode1 = 0x04c0100f, msg_mod0 = 0x04c01073,
                   msg_base4 = 0x04c010cf, msg_texel = 0x04c01133, msg_final = 0x04c0119f;
constexpr uint32_t k_one = 0x04bffd70, k_threshold = 0x04bffd74, k_minus_one = 0x04bffd78;   // 1.0, 1.1, -1.0

using Assert = void(const char *message, const char *function);
using FLog = float(float x, int base);
using Powf = float(float x, float y);

float constant(uint32_t address) { return *rt::ptr<float>(address); }

void check(uint32_t message)
{
    rt::fn<Assert>(assert_thunk)(rt::ptr<const char>(message), rt::ptr<const char>(str_function));
}

__attribute__((target("sse4.1"))) float ceil_ss(float x)
{
    return _mm_cvtss_f32(_mm_round_ss(_mm_set_ss(x), _mm_set_ss(x), _MM_FROUND_TO_POS_INF));
}

float max_ss(float a, float b) { return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(a), _mm_set_ss(b))); }
int32_t truncate(float x) { return _mm_cvttss_si32(_mm_set_ss(x)); }

std::string hex32(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    char b[16];
    std::snprintf(b, sizeof b, "%08x", u);
    return b;
}

std::string bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    char b[16];
    std::snprintf(b, sizeof b, "%02x%02x%02x%02x", u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff, u >> 24);
    return b;
}

}  // namespace

extern "C" void bb_render_yebis_get_recursive_sample_parameters(int32_t base_samples, int32_t mode, int32_t max_passes,
                                                               int32_t *out_passes, int32_t *out_final, float *out_scale,
                                                               float length, float texel)
{
    const int number = rt_capture_begin("render_yebis_get_recursive_sample_parameters");
    std::string imports;
    auto record = [&](const std::string &entry) { imports += (imports.empty() ? "" : ", ") + entry; };
    auto assert_at = [&](uint32_t message) {
        check(message);
        if (number >= 0) record("{\"name\": \"_Assert\", \"argc\": 2}");
    };
    auto flog = [&](float x) {
        const float r = rt::fn<FLog>(flog_thunk)(x, 0);
        if (number >= 0) record("{\"name\": \"_FLog\", \"argc\": 1, \"argf32\": [0], \"ret\": \"0x" + hex32(r) + "\"}");
        return r;
    };
    auto pow_f = [&](float x, float y) {
        const float r = rt::fn<Powf>(powf_thunk)(x, y);
        if (number >= 0) record("{\"name\": \"powf\", \"argc\": 0, \"argf32\": [0, 1], \"ret\": \"0x" + hex32(r) + "\"}");
        return r;
    };

    if (base_samples <= 2 && frame_timing::target_fps() == frame_timing::Target::original)
        assert_at(msg_base3);
    const float base = static_cast<float>(base_samples);
    const int32_t odd = base_samples % 2;
    if (mode == 1) {
        if (odd == 0) assert_at(msg_mod);
        if (base_samples <= 2) assert_at(msg_base3_mode1);
    } else if (mode == 2) {
        if (odd != 0) assert_at(msg_mod0);
        if (base_samples <= 3) assert_at(msg_base4);
    }

    // The texel spacing: clamped down so that one pass does not overshoot the length.
    float spacing = texel;
    bool use_one = false;
    if (texel > constant(k_one) && base * texel >= length) {
        spacing = length / base;
        if (!(spacing > constant(k_one))) use_one = true;
    }
    float divisor;
    if (use_one) {
        divisor = constant(k_one);
    } else if (spacing >= constant(k_one)) {
        divisor = spacing;
    } else {
        assert_at(msg_texel);
        divisor = spacing;
    }

    float ratio = length / divisor + constant(k_one);
    int32_t passes = 0, final_samples = 0;
    float scale = 0.0f;
    if (ratio > constant(k_threshold)) {
        const float log_ratio = flog(ratio);
        const float log_base = flog(base);
        passes = truncate(ceil_ss(max_ss(log_ratio / log_base, constant(k_one))));
        const float total = base * ratio;
        const float power = pow_f(base, static_cast<float>(passes));
        int32_t last = truncate(ceil_ss(total / power));
        if (last <= 0) assert_at(msg_final);
        if (last == 0) last = base_samples;
        int32_t parity;
        if (mode == 1)
            parity = last & 1;
        else
            parity = (~last & 1) | (mode != 2 ? 1 : 0);
        final_samples = (parity ^ 1) + last;
        if (final_samples > base_samples) final_samples = base_samples;
        if (max_passes > 0 && passes > max_passes) {
            passes = max_passes;
            final_samples = base_samples;
        }
        const float power_less = pow_f(base, static_cast<float>(passes - 1));
        scale = length / (power_less * static_cast<float>(final_samples) + constant(k_minus_one));
    }

    if (out_passes) *out_passes = passes;
    if (out_final) *out_final = final_samples;
    if (out_scale) *out_scale = scale;

    if (number >= 0) {
        char head[512];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x00fbc3e0\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"0x%x\", \"rsi\": \"0x%x\", \"rdx\": \"0x%x\", \"rcx\": \"%s\", \"r8\": \"%s\", "
                      "\"r9\": \"%s\", \"xmm0\": \"%s\", \"xmm1\": \"%s\"}, ",
                      number, static_cast<uint32_t>(base_samples), static_cast<uint32_t>(mode), static_cast<uint32_t>(max_passes),
                      out_passes ? "buf:passes" : "0x0", out_final ? "buf:final" : "0x0", out_scale ? "buf:scale" : "0x0",
                      bits(length).c_str(), bits(texel).c_str());
        const std::string json = std::string(head) +
            "\"buffers\": {\"passes\": {\"size\": 4}, \"final\": {\"size\": 4}, \"scale\": {\"size\": 4}}, "
            "\"memory\": [], \"stubs\": [], \"imports\": [" + imports + "]}";
        rt_capture_write("render_yebis_get_recursive_sample_parameters", number, json.c_str());
    }
}
