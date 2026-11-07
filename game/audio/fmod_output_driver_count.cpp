// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD output callback: write one driver when the output is present.
// Installed by 0x0111aed0 in the FMOD Orbis AudioOut output descriptor.
// Its descriptor slot corresponds to FMOD getnumdrivers; the callback reports one driver.
#include <cstdint>
#include <cstring>

extern "C" uint64_t bb_audio_fmod_output_driver_count(uint64_t opaque0, void *output)
{
    (void)opaque0;
    if (output != nullptr) {
        const uint32_t word = 1;
        std::memcpy(output, &word, sizeof(word));
    }
    return 0;
}
