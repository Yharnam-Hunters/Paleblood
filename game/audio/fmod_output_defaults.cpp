// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD output callback: write 132, 48000 and 6 through optional outputs in that order.
// Installed by 0x0111aed0 in the FMOD Orbis AudioOut output descriptor.
// 48000 may be a sample rate and 6 a 5.1 speaker mode; these meanings are hypotheses.
#include <cstdint>
#include <cstring>

extern "C" uint64_t bb_audio_fmod_output_defaults(uint64_t opaque0, uint64_t opaque1,
                                            void *output0, void *output1, void *output2)
{
    (void)opaque0;
    (void)opaque1;
    if (output0 != nullptr) {
        const uint32_t word0 = 0x84;
        std::memcpy(output0, &word0, sizeof(word0));
    }
    if (output1 != nullptr) {
        const uint32_t word1 = 0xbb80;
        std::memcpy(output1, &word1, sizeof(word1));
    }
    if (output2 != nullptr) {
        const uint32_t word2 = 6;
        std::memcpy(output2, &word2, sizeof(word2));
    }
    return 0;
}
