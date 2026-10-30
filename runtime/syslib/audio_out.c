/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. Audio output start-up (libSceAudioOut): initializing the library and
   opening an output port.

   Facts used: the OpenOrbis homebrew SDK's AudioOut.h declares int32_t sceAudioOutInit(void) and
   int32_t sceAudioOutOpen(user, port type, index, length, sample rate, param); its _types headers
   give port type 0 (main), format 4 (float stereo), and the error codes 0x8026000E (already
   initialized) and 0x8026000F (not initialized). The executable's only init call (0x0111b665)
   accepts success or exactly 0x8026000E. Its only open (0x0111b69d) passes user 0xff (the system
   user in the SDK's user types), the main port, index 0, 256 samples, 48000 Hz and format 4 (or 5
   for eight channels), and keeps a non-negative result as the port handle.

   The runtime records each open port and hands out handles from 1; sound output belongs to the
   output calls, which stop by name until they are implemented. What is not established stops by
   name: any open other than the observed one (format 5 is not in the public list), and running
   out of handles. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>

#define SCE_AUDIO_OUT_ERROR_ALREADY_INIT ((int32_t)0x8026000Eu)
#define SCE_AUDIO_OUT_ERROR_NOT_INIT ((int32_t)0x8026000Fu)
enum {
    USER_SYSTEM = 0xff,
    PORT_TYPE_MAIN = 0,
    FORMAT_FLOAT_STEREO = 4,
    OBSERVED_LENGTH = 256,
    OBSERVED_RATE = 48000,
    MAX_PORTS = 8,
};

typedef struct {
    uint32_t length, rate, format;
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
    if (user != USER_SYSTEM || port_type != PORT_TYPE_MAIN || index != 0)
        rt_fatal(fn, "a user, port type or index other than the observed system user's main port 0");
    if (length != OBSERVED_LENGTH || rate != OBSERVED_RATE || format != FORMAT_FLOAT_STEREO)
        rt_fatal(fn, "a length, rate or format other than the observed 256 samples at 48000 Hz, float stereo");
    const int handle = atomic_fetch_add(&ports_opened, 1) + 1; /* handles from 1 */
    if (handle > MAX_PORTS) rt_fatal(fn, "more ports than the runtime hands out handles for");
    ports[handle] = (audio_port){length, rate, format};
    return handle;
}

RT_SYSLIB("libSceAudioOut", sceAudioOutInit, audio_out_init);
RT_SYSLIB("libSceAudioOut", sceAudioOutOpen, audio_out_open);
