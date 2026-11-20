// SPDX-License-Identifier: GPL-2.0-or-later
// The FMOD output owner's sceAudioOut ports and mix buffer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// A prefix view of the same owner as OutputThreadContext (output_threads.h), kept
// separate so the verified functions that include that header keep their results;
// merge the two when those are recorded again.
struct OutputPortContext {
    std::byte unknown_0x0000[0x6ac];
    int32_t audio_out_port;  // sceAudioOut port handle; zero when closed
    std::byte unknown_0x06b0[0x18];
    void *mix_buffer;  // released with the ports
    std::byte unknown_0x06d0[0x2b0];  // OutputThreadContext's two Thread objects
    // A second sceAudioOut port, closed together with the first; its purpose is unknown.
    int32_t second_audio_out_port;
};
static_assert(offsetof(OutputPortContext, audio_out_port) == 0x6ac);
static_assert(offsetof(OutputPortContext, mix_buffer) == 0x6c8);
static_assert(offsetof(OutputPortContext, second_audio_out_port) == 0x980);

}  // namespace audio
