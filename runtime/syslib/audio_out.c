/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. Audio output start-up (libSceAudioOut): initializing the library,
   opening an output port and setting its volumes.

   Facts used: the OpenOrbis homebrew SDK's AudioOut.h declares int32_t sceAudioOutInit(void) and
   int32_t sceAudioOutOpen(user, port type, index, length, sample rate, param); its _types headers
   give port type 0 (main), format 4 (float stereo), and the error codes 0x8026000E (already
   initialized) and 0x8026000F (not initialized). The executable's only init call (0x0111b665)
   accepts success or exactly 0x8026000E. Its only open (0x0111b69d) passes user 0xff (the system
   user in the SDK's user types), the main port, index 0, 256 samples, 48000 Hz and a format, and
   keeps a non-negative result as the port handle. The format is the executable's own evidence: its
   output callback (0x0111b600) accepts 2 or 8 channels and passes 4 | (channels == 8), so 4 is float
   stereo (as in the SDK) and 5 is float with eight channels; the boot opens eight channels. A
   second open (0x0111b6fc) passes port type 1 with the same settings and does not check the
   result; the PS4 name of type 1 is not public (the sibling Vita SDK calls it BGM).

   The runtime records each open port and hands out handles from 1; sound output belongs to the
   output calls, which stop by name until they are implemented. What is not established stops by
   name: any open other than the observed ones, and running out of handles. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>

#define SCE_AUDIO_OUT_ERROR_ALREADY_INIT ((int32_t)0x8026000Eu)
#define SCE_AUDIO_OUT_ERROR_NOT_INIT ((int32_t)0x8026000Fu)
enum {
    USER_SYSTEM = 0xff,
    PORT_TYPE_MAIN = 0,
    PORT_TYPE_SECOND_SEEN = 1,
    FORMAT_FLOAT_STEREO = 4,
    FORMAT_FLOAT_8CH = 5,
    OBSERVED_LENGTH = 256,
    OBSERVED_RATE = 48000,
    MAX_PORTS = 8,
};

enum { MAX_CHANNELS = 8, VOLUME_LARGEST_SEEN = 32768 };

typedef struct {
    uint32_t length, rate, format, channels;
    int32_t volume[MAX_CHANNELS];
} audio_port;

static atomic_int initialized;
static atomic_int ports_opened;
static audio_port ports[MAX_PORTS + 1];

static int32_t audio_out_init(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&initialized, &expected, 1) ? 0 : SCE_AUDIO_OUT_ERROR_ALREADY_INIT;
}

static int32_t audio_out_open(int32_t user, int32_t port_type, int32_t index, uint32_t length, uint32_t rate,
                              uint32_t format)
{
    static const char fn[] = "sceAudioOutOpen";
    if (!atomic_load(&initialized)) return SCE_AUDIO_OUT_ERROR_NOT_INIT;
    if (user != USER_SYSTEM || (port_type != PORT_TYPE_MAIN && port_type != PORT_TYPE_SECOND_SEEN) || index != 0)
        rt_fatal(fn, "a user, port type or index other than the observed system user's port types 0 and 1, index 0");
    if (length != OBSERVED_LENGTH || rate != OBSERVED_RATE || (format != FORMAT_FLOAT_STEREO && format != FORMAT_FLOAT_8CH))
        rt_fatal(fn, "a length, rate or format other than the observed 256 float samples at 48000 Hz, 2 or 8 channels");
    const int handle = atomic_fetch_add(&ports_opened, 1) + 1; /* handles from 1 */
    if (handle > MAX_PORTS) rt_fatal(fn, "more ports than the runtime hands out handles for");
    ports[handle] = (audio_port){length, rate, format, format == FORMAT_FLOAT_8CH ? 8 : 2, {0}};
    return handle;
}

/* int32_t sceAudioOutSetVolume(int32_t handle, int32_t flag, int32_t *volume), as the SDK's AudioOut.h
   declares it. The executable's only call (0x0111b6d6) passes its new port, flag 0xff and eight
   volumes of 32768, and treats nonzero as failure: flag bit n selects channel n, whose volume is
   volume[n]. The runtime records them. A handle it did not hand out, a flag naming a channel the
   port does not have, a missing array and a volume outside 0..32768 stop by name. */
static int32_t audio_out_set_volume(int32_t handle, int32_t flag, const int32_t *volume)
{
    static const char fn[] = "sceAudioOutSetVolume";
    if (handle < 1 || handle > atomic_load(&ports_opened))
        rt_fatal(fn, "a handle the runtime did not hand out: the error value is not established");
    audio_port *const port = &ports[handle];
    if (!volume || !flag || (uint32_t)flag >> port->channels)
        rt_fatal(fn, "no volumes, or a flag naming a channel the port does not have: the answer is not established");
    for (uint32_t channel = 0; channel < port->channels; channel++) {
        if (!(flag & (1 << channel))) continue;
        if (volume[channel] < 0 || volume[channel] > VOLUME_LARGEST_SEEN)
            rt_fatal(fn, "a volume outside 0..32768: the answer is not established");
        port->volume[channel] = volume[channel];
    }
    return 0;
}

RT_SYSLIB("libSceAudioOut", sceAudioOutInit, audio_out_init);
RT_SYSLIB("libSceAudioOut", sceAudioOutOpen, audio_out_open);
RT_SYSLIB("libSceAudioOut", sceAudioOutSetVolume, audio_out_set_volume);
