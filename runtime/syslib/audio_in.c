/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. The microphone input (libSceAudioIn), as a console with no microphone
   connected: a port opens and reads silence.

   The executable's only user is FMOD's PS4 record driver. Its record start (0x0111c050) picks the
   Nth logged-in user, opens a port with sceAudioInOpen(user, type 0, 1 or 5, index 0, 256 samples,
   16000 Hz, param 0 = 16-bit mono) at 0x0111c11d and treats a negative answer as an FMOD error.
   Its update thread (0x0111be90) calls sceAudioInInput(handle, buffer) at 0x0111beee and uses the
   answer as a sample count to advance its ring buffer, so a valid handle must never get an error
   there. The record stop closes the port at 0x0111c25b, ignoring the answer.

   Facts used (shadPS4's audioin.cpp and audioin_error.h, facts only; the same codes as the PS Vita
   SDK's audioin.h): a port opens without a device and reads silence (shadPS4's null input; the
   PS4Recomp and droogie bbhost runtimes also open and read zeroes; a family of native ports (the "native runtime" family)
   answers 0x80260109 "not opened" instead, so the majority answer is used); len must be 1 to 2048
   samples (0x80260102), the frequency 16000 or 48000 (0x80260103), param 0 (16-bit mono) or 1
   (16-bit stereo) (0x80260106); an unknown handle is 0x80260101 and a NULL buffer 0x80260105.

   What the runtime does: Open checks those, hands out a handle from 1, and remembers the port's
   size; Input writes len samples of silence, waits for the time they last (len / frequency, as the
   output side paces itself) and answers len; Close frees the handle and answers 0. A port type
   other than 0, 1 and 5, an index other than 0, and more ports than the table holds stop by name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define AUDIO_IN_ERROR_INVALID_HANDLE ((int32_t)0x80260101u)
#define AUDIO_IN_ERROR_INVALID_SIZE ((int32_t)0x80260102u)
#define AUDIO_IN_ERROR_INVALID_FREQ ((int32_t)0x80260103u)
#define AUDIO_IN_ERROR_INVALID_POINTER ((int32_t)0x80260105u)
#define AUDIO_IN_ERROR_INVALID_PARAM ((int32_t)0x80260106u)
enum { MAX_PORTS = 8, MAX_SAMPLES = 2048, FREQ_LOW = 16000, FREQ_HIGH = 48000 };
enum { TYPE_VOICE_CHAT = 0, TYPE_GENERAL = 1, TYPE_VOICE_RECOGNITION = 5 };
enum { PARAM_S16_MONO = 0, PARAM_S16_STEREO = 1, BYTES_PER_SAMPLE = 2 };

typedef struct {
    int open;
    uint32_t samples, frequency, channels;
} port;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static port ports[MAX_PORTS]; /* handle h is ports[h - 1] */

static int32_t audio_in_open(int32_t user, uint32_t type, uint32_t index, uint32_t samples, uint32_t frequency, uint32_t param)
{
    static const char fn[] = "sceAudioInOpen";
    (void)user;
    if (type != TYPE_VOICE_CHAT && type != TYPE_GENERAL && type != TYPE_VOICE_RECOGNITION)
        rt_fatal(fn, "a port type other than 0, 1 and 5: the answer is not established");
    if (index != 0) rt_fatal(fn, "a device index other than 0: the answer is not established");
    if (samples == 0 || samples > MAX_SAMPLES) return AUDIO_IN_ERROR_INVALID_SIZE;
    if (param != PARAM_S16_MONO && param != PARAM_S16_STEREO) return AUDIO_IN_ERROR_INVALID_PARAM;
    if (frequency != FREQ_LOW && frequency != FREQ_HIGH) return AUDIO_IN_ERROR_INVALID_FREQ;
    pthread_mutex_lock(&lock);
    int slot = 0;
    while (slot < MAX_PORTS && ports[slot].open) slot++;
    if (slot < MAX_PORTS) ports[slot] = (port){1, samples, frequency, param == PARAM_S16_STEREO ? 2u : 1u};
    pthread_mutex_unlock(&lock);
    if (slot == MAX_PORTS) rt_fatal(fn, "more ports than the runtime's table holds");
    return slot + 1;
}

/* The port behind a handle, copied out, or 0 when the handle names no open port. */
static int port_of(int32_t handle, port *out)
{
    pthread_mutex_lock(&lock);
    const int found = handle >= 1 && handle <= MAX_PORTS && ports[handle - 1].open;
    if (found) *out = ports[handle - 1];
    pthread_mutex_unlock(&lock);
    return found;
}

static int32_t audio_in_input(int32_t handle, void *buffer)
{
    port p;
    if (!port_of(handle, &p)) return AUDIO_IN_ERROR_INVALID_HANDLE;
    if (!buffer) return AUDIO_IN_ERROR_INVALID_POINTER;
    memset(buffer, 0, (size_t)p.samples * p.channels * BYTES_PER_SAMPLE);
    const uint64_t nanoseconds = (uint64_t)p.samples * 1000000000u / p.frequency;
    const struct timespec wait = {(time_t)(nanoseconds / 1000000000u), (long)(nanoseconds % 1000000000u)};
    nanosleep(&wait, NULL);
    return (int32_t)p.samples;
}

static int32_t audio_in_close(int32_t handle)
{
    pthread_mutex_lock(&lock);
    const int found = handle >= 1 && handle <= MAX_PORTS && ports[handle - 1].open;
    if (found) ports[handle - 1].open = 0;
    pthread_mutex_unlock(&lock);
    return found ? 0 : AUDIO_IN_ERROR_INVALID_HANDLE;
}

RT_SYSLIB("libSceAudioIn", sceAudioInOpen, audio_in_open);
RT_SYSLIB("libSceAudioIn", sceAudioInInput, audio_in_input);
RT_SYSLIB("libSceAudioIn", sceAudioInClose, audio_in_close);
