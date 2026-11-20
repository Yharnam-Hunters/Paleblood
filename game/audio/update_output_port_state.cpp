// SPDX-License-Identifier: GPL-2.0-or-later
// Poll the FMOD output's second sceAudioOut port and tell the listener when its output
// connection appears or disappears. The output callback audio_fmod_update_output_port_state
// adjusts its pointer and calls this; it always returns zero.
#include "output_ports.h"
#include "runtime/original.h"

namespace {
// The SDK's 32-byte port state; only the connected-output bits are read here.
struct AudioOutPortState {
    uint16_t output;  // zero when nothing is connected
    std::byte unknown_0x0002[0x1e];
};
static_assert(sizeof(AudioOutPortState) == 0x20);

// The executable's call stub for sceAudioOutGetPortState.
RT_ORIGINAL(0x02fbff78, sce_audio_out_get_port_state,
            int32_t(int32_t port, AudioOutPortState *state));
// Two listener updates taking the new state and a flag this caller always sets; a bit in the
// service context's flags picks one. What separates them is not established.
RT_ORIGINAL(0x01134090, update_listener_flagged,
            void(audio::OutputPortListener *, bool disconnected, bool flag));
RT_ORIGINAL(0x01133f80, update_listener_unflagged,
            void(audio::OutputPortListener *, bool disconnected, bool flag));

constexpr uint8_t listener_update_flag = 0x20;
}  // namespace

extern "C" uint32_t bb_audio_fmod_output_update_port_state(audio::OutputPortContext *owner)
{
    if (owner->listener == nullptr || owner->second_audio_out_port == 0)
        return 0;

    // The original ignores the call's result and reads the state either way. It also leaves
    // the buffer uninitialised; zero-filling it here means a failed call reads as
    // disconnected rather than as leftover stack bytes.
    AudioOutPortState state{};
    sce_audio_out_get_port_state(owner->second_audio_out_port, &state);
    const bool disconnected = state.output == 0;

    if (owner->second_port_disconnected == static_cast<uint8_t>(disconnected))
        return 0;
    owner->second_port_disconnected = disconnected;

    if (owner->service_context->flags_32 & listener_update_flag)
        update_listener_flagged(owner->listener, disconnected, true);
    else
        update_listener_unflagged(owner->listener, disconnected, true);
    return 0;
}
