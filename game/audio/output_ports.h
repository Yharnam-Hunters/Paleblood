// SPDX-License-Identifier: GPL-2.0-or-later
// The FMOD output owner's sceAudioOut ports and mix buffer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// The object that OutputThreadContext::service_context (output_threads.h) points to; its
// type is unknown, and only the flag byte at +0x32 is read here.
struct OutputServiceContext {
    std::byte unknown_0x0000[0x32];
    uint8_t flags_32;
};
static_assert(offsetof(OutputServiceContext, flags_32) == 0x32);

// Told when the second port's output connection changes; its type is unknown.
struct OutputPortListener;

// A prefix view of the same owner as OutputThreadContext (output_threads.h), kept
// separate so the verified functions that include that header keep their results;
// merge the two when those are recorded again.
struct OutputPortContext {
    std::byte unknown_0x0000[0x60];
    const OutputServiceContext *service_context;
    std::byte unknown_0x0068[8];
    OutputPortListener *listener;
    std::byte unknown_0x0078[0x634];
    int32_t audio_out_port;  // sceAudioOut port handle; zero when closed
    std::byte unknown_0x06b0[0x18];
    void *mix_buffer;  // released with the ports
    std::byte unknown_0x06d0[0x2b0];  // OutputThreadContext's two Thread objects
    // A second sceAudioOut port, closed together with the first; its purpose is unknown.
    int32_t second_audio_out_port;
    // Whether the second port last reported no output connected (any byte other than 0 or 1
    // counts as changed).
    uint8_t second_port_disconnected;
};
static_assert(offsetof(OutputPortContext, service_context) == 0x60);
static_assert(offsetof(OutputPortContext, listener) == 0x70);
static_assert(offsetof(OutputPortContext, audio_out_port) == 0x6ac);
static_assert(offsetof(OutputPortContext, mix_buffer) == 0x6c8);
static_assert(offsetof(OutputPortContext, second_audio_out_port) == 0x980);
static_assert(offsetof(OutputPortContext, second_port_disconnected) == 0x984);

}  // namespace audio
