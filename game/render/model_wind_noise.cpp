// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the fractal noise the model wind (model_wind.cpp) samples for its gusts and its
// jittered direction.
// - render_fractal_noise_02a85e30: the sum of the octaves of the base noise (0x02a85950, kept as
//   the original). The first octave samples the point as given at amplitude one half; each next
//   one samples it scaled by the frequency step once more, at the amplitude times the
//   persistence once more. Zero octaves give +0. The count is taken as unsigned: a negative one
//   runs about four billion octaves, as the original's count-down loop does. Single-precision
//   steps in the original's order (no fused multiply-add), so the sum is the same bit for bit.
#include <cstdint>

#include "../engine/scalar.h"
#include "model_wind.h"

namespace {

using render::wind::NoiseState;

constexpr float first_octave_scale = 1.0f;
constexpr float first_octave_amplitude = 0.5f;

// The base noise at a point (it starts from the coordinates' floors). It never reads the state
// pointer; the original passes it to the first sample only (the register is not kept across the
// calls), this passes it to every sample.
RT_ORIGINAL(0x02a85950, base_noise, float(NoiseState *, float x, float y, float z));

}  // namespace

extern "C" float bb_render_fractal_noise_02a85e30(float x, float y, float z, float frequency_step, float persistence,
                                                   NoiseState *noise, int32_t octaves)
{
    float scale = first_octave_scale;
    float amplitude = first_octave_amplitude;
    float sum = 0.0f;
    for (uint32_t left = static_cast<uint32_t>(octaves); left != 0; --left) {
        const float sample =
            base_noise(noise, engine::multiply(scale, x), engine::multiply(scale, y), engine::multiply(scale, z));
        sum = engine::add(sum, engine::multiply(amplitude, sample));
        scale = engine::multiply(scale, frequency_step);
        amplitude = engine::multiply(amplitude, persistence);
    }
    return sum;
}
