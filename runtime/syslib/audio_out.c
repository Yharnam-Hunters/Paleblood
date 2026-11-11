/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. Audio output (libSceAudioOut): initializing the library, opening an
   output port, setting its volumes and handing it sound.

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

   The movie player's sound has a port of its own (opened at 0x02c225ae and 0x02c22842): 1024
   samples at 48000 Hz, its format chosen from the movie's channel count: 0, 1 or 2 (16-bit mono,
   stereo, eight channels) for 48 kHz sound, 3, 4 or 5 (float) otherwise. Those numbers are the
   SDK's (_types/audio_out.h names 0, 1, 3 and 4; shadPS4's audioout.h adds 2 and 5 as the
   eight-channel forms).

   The runtime records each open port and hands out handles from 1, the lowest one free, so a closed
   port's handle is handed out again (shadPS4's audioout.cpp also reuses a closed port's slot). What is not established stops by
   name: any open other than the observed ones (256 float samples in formats 4 and 5, or 1024 samples
   in formats 0 to 5, at 48000 Hz), and running out of handles. */
#include "runtime/audio_host.h"
#include "runtime/host_time.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#define SCE_AUDIO_OUT_ERROR_ALREADY_INIT ((int32_t)0x8026000Eu)
#define SCE_AUDIO_OUT_ERROR_NOT_INIT ((int32_t)0x8026000Fu)
enum {
    USER_SYSTEM = 0xff,
    PORT_TYPE_MAIN = 0,
    PORT_TYPE_SECOND_SEEN = 1,
    FORMAT_S16_8CH = 2,
    FORMAT_FLOAT_STEREO = 4,
    FORMAT_FLOAT_8CH = 5,
    OBSERVED_LENGTH = 256,
    MOVIE_LENGTH = 1024,
    OBSERVED_RATE = 48000,
    MAX_PORTS = 8,
};

enum { MAX_CHANNELS = 8, VOLUME_LARGEST_SEEN = 32768 };

typedef struct {
    uint32_t length, rate, format, channels;
    int32_t volume[MAX_CHANNELS];
    pthread_mutex_t lock;
    struct timespec played; /* when the buffer handed over last finishes playing (monotonic) */
    void *host;             /* the host stream, with --audio host */
    int volume_set;         /* until the title sets volumes, the port plays at 0 dB */
    int open;
} audio_port;

static atomic_int initialized;
static pthread_mutex_t ports_lock = PTHREAD_MUTEX_INITIALIZER; /* handing out and taking back handles */
static audio_port ports[MAX_PORTS + 1];

/* Whether a handle names an open port. */
static int port_open(int32_t handle)
{
    if (handle < 1 || handle > MAX_PORTS) return 0;
    pthread_mutex_lock(&ports_lock);
    const int open = ports[handle].open;
    pthread_mutex_unlock(&ports_lock);
    return open;
}

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
    const int main_port = length == OBSERVED_LENGTH && (format == FORMAT_FLOAT_STEREO || format == FORMAT_FLOAT_8CH);
    const int movie_port = length == MOVIE_LENGTH && format <= FORMAT_FLOAT_8CH;
    if (rate != OBSERVED_RATE || (!main_port && !movie_port))
        rt_fatal(fn, "a length, rate or format other than the observed: 256 float samples (2 or 8 channels), or 1024 samples "
                     "in formats 0 to 5, at 48000 Hz");
    static const uint32_t channels_of[] = {1, 2, 8, 1, 2, 8}; /* by format: mono, stereo, eight channels */
    void *host = NULL;
    if (rt_audio_host_enabled()) {
        char error[160];
        host = rt_audio_host_open(channels_of[format], rate, error, sizeof error);
        if (!host) rt_fatal(fn, error);
    }
    pthread_mutex_lock(&ports_lock);
    int handle = 1; /* the lowest free, from 1 */
    while (handle <= MAX_PORTS && ports[handle].open) handle++;
    if (handle > MAX_PORTS) rt_fatal(fn, "more open ports than the runtime hands out handles for");
    ports[handle] = (audio_port){length, rate, format, channels_of[format], {0}, PTHREAD_MUTEX_INITIALIZER, {0, 0}, host, 0, 1};
    pthread_mutex_unlock(&ports_lock);
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
    if (!port_open(handle)) rt_fatal(fn, "a handle the runtime did not hand out: the error value is not established");
    audio_port *const port = &ports[handle];
    if (!volume || !flag || (uint32_t)flag >> port->channels)
        rt_fatal(fn, "no volumes, or a flag naming a channel the port does not have: the answer is not established");
    for (uint32_t channel = 0; channel < port->channels; channel++) {
        if (!(flag & (1 << channel))) continue;
        if (volume[channel] < 0 || volume[channel] > VOLUME_LARGEST_SEEN)
            rt_fatal(fn, "a volume outside 0..32768: the answer is not established");
        port->volume[channel] = volume[channel];
    }
    port->volume_set = 1;
    return 0;
}

/* int32_t sceAudioOutOutput(int32_t handle, const void *samples), as the SDK's AudioOut.h declares
   it. The executable's audio thread (0x0111b93c) hands its port one block at a time (length samples
   for every channel), advances its ring buffer when the result is not negative, and loops with no
   wait of its own: the call itself paces the thread, returning once the port has room for the next
   block. The runtime plays nothing yet: each port takes one block per length / rate seconds (256
   samples at 48000 Hz: 5.33 ms), and a call waits until the block before it would have finished
   playing, then answers 0 (the caller only checks the sign). A port that falls behind starts again
   from now. A handle the runtime did not hand out stops by name.

   No block (samples NULL) hands nothing over: the call only waits until the port has room for the
   next block, as with a block, and answers 0 without moving the port's timeline on. shadPS4's
   audioout.cpp waits for the port's buffer to drain and copies nothing for a null pointer, returning
   0 samples; PS4Recomp's ps4_audioout.c waits for room the same way and pushes nothing, returning 0.
   The movie player's sound thread calls it so when it has no sound to give.

   With --audio host (runtime/audio_host.h) the block is played on the host instead: each channel
   scaled by its volume (32768 is 0 dB, the level the title sets; until it sets one the port plays at
   0 dB, the runtime's choice), queued on the port's host stream, and the call returns once no more
   than a few blocks wait to be played, so the title's audio thread runs at the device's pace. */
enum { NANOSECONDS_PER_SECOND = 1000000000, VOLUME_UNITY = 32768, HOST_BLOCKS_AHEAD = 4, HOST_WAIT_LIMIT_MS = 2000 };

/* One sample of a block as a float: 16-bit formats (0 to 2) scaled to -1..1, float formats as given. */
static float sample_at(const audio_port *port, const void *samples, uint32_t index)
{
    if (port->format <= FORMAT_S16_8CH) return (float)((const int16_t *)samples)[index] / 32768.0f;
    return ((const float *)samples)[index];
}

/* Waits until no more than a few blocks wait to be played on the port's host stream. */
static void wait_for_host_room(const char *fn, audio_port *port)
{
    const struct timespec millisecond = {0, NANOSECONDS_PER_SECOND / 1000};
    for (int waited = 0; rt_audio_host_queued_frames(port->host) > HOST_BLOCKS_AHEAD * port->length; waited++) {
        if (waited == HOST_WAIT_LIMIT_MS) rt_fatal(fn, "the host audio device stopped playing");
        nanosleep(&millisecond, NULL);
    }
}

static void play_on_host(const char *fn, audio_port *port, const void *samples)
{
    static float scaled[MOVIE_LENGTH * MAX_CHANNELS]; /* not on the title's thread stack; one port at a time */
    static pthread_mutex_t scaled_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&scaled_lock);
    for (uint32_t frame = 0; frame < port->length; frame++) {
        for (uint32_t channel = 0; channel < port->channels; channel++) {
            const float gain = port->volume_set ? (float)port->volume[channel] / VOLUME_UNITY : 1.0f;
            const uint32_t index = frame * port->channels + channel;
            scaled[index] = sample_at(port, samples, index) * gain;
        }
    }
    const int written = rt_audio_host_write(port->host, scaled, port->length);
    pthread_mutex_unlock(&scaled_lock);
    if (written) rt_fatal(fn, "the host refused the sound");
    wait_for_host_room(fn, port);
}

static int64_t nanoseconds(const struct timespec *t) { return (int64_t)t->tv_sec * NANOSECONDS_PER_SECOND + t->tv_nsec; }

static int32_t audio_out_output(int32_t handle, const void *samples)
{
    static const char fn[] = "sceAudioOutOutput";
    if (!port_open(handle)) rt_fatal(fn, "a handle the runtime did not hand out: the error value is not established");
    audio_port *const port = &ports[handle];
    if (port->host) {
        pthread_mutex_lock(&port->lock);
        if (samples) play_on_host(fn, port, samples);
        else wait_for_host_room(fn, port);
        pthread_mutex_unlock(&port->lock);
        return 0;
    }
    const int64_t block = (int64_t)port->length * NANOSECONDS_PER_SECOND / port->rate;
    pthread_mutex_lock(&port->lock);
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        pthread_mutex_unlock(&port->lock);
        rt_fatal(fn, "the host monotonic clock read failed");
    }
    int64_t wait_until = nanoseconds(&port->played);
    if (wait_until > nanoseconds(&now)) {
        if (rt_host_sleep_until_monotonic(&port->played)) {
            pthread_mutex_unlock(&port->lock);
            rt_fatal(fn, "the host monotonic wait failed");
        }
    } else
        wait_until = nanoseconds(&now); /* idle or behind: this block starts now */
    if (!samples) { /* room for the next block, and nothing handed over */
        pthread_mutex_unlock(&port->lock);
        return 0;
    }
    const int64_t done = wait_until + block;
    port->played = (struct timespec){done / NANOSECONDS_PER_SECOND, done % NANOSECONDS_PER_SECOND};
    pthread_mutex_unlock(&port->lock);
    return 0;
}

/* int32_t sceAudioOutGetPortState(int32_t handle, void *state), as the SDK's AudioOut.h declares it;
   its _types/audio_out.h gives the state's layout (32 bytes): a 16-bit output, an 8-bit channel
   count, a volume and a 64-bit flag. The executable's only call (0x0111bb41) asks about its second
   port and reads only output, testing it for 0. The runtime plays nothing on the host, so no port
   is output anywhere: it answers output 0, and with --audio host 1 (CONNECTED_PRIMARY, shadPS4's
   audioout.h) for a port playing on the host; 0 in the fields whose encoding is not established. An unknown handle and a missing state stop by
   name. */
enum { PORT_STATE_SIZE = 32, OUTPUT_CONNECTED_PRIMARY = 0x1 };

static int32_t audio_out_get_port_state(int32_t handle, void *state)
{
    static const char fn[] = "sceAudioOutGetPortState";
    if (!port_open(handle)) rt_fatal(fn, "a handle the runtime did not hand out: the error value is not established");
    if (!state) rt_fatal(fn, "no state: the error value is not established");
    memset(state, 0, PORT_STATE_SIZE);
    const uint16_t output = ports[handle].host ? OUTPUT_CONNECTED_PRIMARY : 0; /* heard on the host, or nowhere */
    memcpy(state, &output, sizeof output);
    return 0;
}

/* int32_t sceAudioOutClose(int32_t handle), as the SDK's AudioOut.h declares it. The movie player's
   sound ends with one (0x02c2276f): it first hands its port no block (sceAudioOutOutput with NULL,
   which waits for room), then closes the port and takes a negative result as failure. The runtime
   takes the port back, after any output call on it has returned, closes its host stream and answers
   0; the handle is free for the next open. shadPS4's audioout.cpp also frees the port and answers 0.
   A handle that names no open port stops by name (the error value is not established). */
static int32_t audio_out_close(int32_t handle)
{
    static const char fn[] = "sceAudioOutClose";
    if (!port_open(handle)) rt_fatal(fn, "a handle that names no open port: the error value is not established");
    audio_port *const port = &ports[handle];
    pthread_mutex_lock(&port->lock); /* an output call on the port finishes first */
    void *const host = port->host;
    port->host = NULL;
    pthread_mutex_unlock(&port->lock);
    if (host) rt_audio_host_close(host);
    pthread_mutex_lock(&ports_lock);
    port->open = 0;
    pthread_mutex_unlock(&ports_lock);
    return 0;
}

RT_SYSLIB("libSceAudioOut", sceAudioOutInit, audio_out_init);
RT_SYSLIB("libSceAudioOut", sceAudioOutClose, audio_out_close);
RT_SYSLIB("libSceAudioOut", sceAudioOutGetPortState, audio_out_get_port_state);
RT_SYSLIB("libSceAudioOut", sceAudioOutOutput, audio_out_output);
RT_SYSLIB("libSceAudioOut", sceAudioOutOpen, audio_out_open);
RT_SYSLIB("libSceAudioOut", sceAudioOutSetVolume, audio_out_set_volume);
