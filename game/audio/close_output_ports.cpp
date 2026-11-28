// SPDX-License-Identifier: GPL-2.0-or-later
// Close the FMOD output's sceAudioOut ports and release its mix buffer. The output
// callback audio_fmod_close_output_ports adjusts its pointer and tail-calls this.
#include "output_ports.h"
#include "thread_services.h"

namespace {
// The executable's call stubs for sceAudioOutOutput and sceAudioOutClose.
RT_ORIGINAL(0x02fbff58, sce_audio_out_output, int32_t(int32_t port, const void *samples));
RT_ORIGINAL(0x02fbff68, sce_audio_out_close, int32_t(int32_t port));
RT_GLOBAL(0x04c7d18b, output_source_file, const char);

constexpr int32_t original_close_source_line = 0x107;
// The same status the output's thread helpers return on failure.
constexpr uint32_t port_error = 0x21;
}  // namespace

extern "C" uint32_t bb_audio_fmod_output_close(audio::OutputPortContext *owner)
{
    if (owner->audio_out_port != 0) {
        // Output with no samples blocks until the queued buffer has played.
        if (sce_audio_out_output(owner->audio_out_port, nullptr) < 0)
            return port_error;
        if (sce_audio_out_close(owner->audio_out_port) != 0)
            return port_error;
        owner->audio_out_port = 0;
        // Only closed when the first port was open, and without checking it is open itself.
        if (sce_audio_out_close(owner->second_audio_out_port) != 0)
            return port_error;
        owner->second_audio_out_port = 0;
    }

    if (owner->mix_buffer != nullptr) {
        audio::release_thread_storage(audio::thread_services.get()->memory_pool, owner->mix_buffer,
                                      output_source_file.address(), original_close_source_line);
        owner->mix_buffer = nullptr;
    }
    return 0;
}
