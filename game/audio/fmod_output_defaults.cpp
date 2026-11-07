// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD output callback: write 132, 48000 and 6 through optional outputs in that order.
// Installed by 0x0111aed0 in the FMOD Orbis AudioOut output descriptor.
// 48000 may be a sample rate and 6 a 5.1 speaker mode; these meanings are hypotheses.
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t default_output_word = 0x84; // The meaning of this output is unknown.
// Sample rate and speaker mode meanings are hypotheses from the FMOD descriptor.
constexpr uint32_t sample_rate_hz = 48000;
constexpr uint32_t speaker_mode_5_1 = 6;
}

extern "C" uint64_t bb_audio_fmod_output_defaults(uint64_t opaque0, uint64_t opaque1,
                                            void *output0, void *output1, void *output2)
{
    (void)opaque0;
    (void)opaque1;
    if (output0 != nullptr) {
        const uint32_t word0 = default_output_word;
        std::memcpy(output0, &word0, sizeof(word0));
    }
    if (output1 != nullptr) {
        const uint32_t word1 = sample_rate_hz;
        std::memcpy(output1, &word1, sizeof(word1));
    }
    if (output2 != nullptr) {
        const uint32_t word2 = speaker_mode_5_1;
        std::memcpy(output2, &word2, sizeof(word2));
    }
    return 0;
}
