/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Sound on the host (runtime/audio/): what the title's audio output ports play, through SDL3 when
   the runtime is built with it and pbboot runs with --audio host. Off, nothing is played and the
   ports keep time on their own (runtime/syslib/audio_out.c). */
#ifndef RUNTIME_AUDIO_HOST_H
#define RUNTIME_AUDIO_HOST_H

#include <stddef.h>
#include <stdint.h>

/* Turns host sound on or off, before any port opens. 0, or -1 when the build has no host sound. */
int rt_audio_host_configure(int enabled);
int rt_audio_host_enabled(void);

/* A host stream for interleaved 32-bit float frames of this many channels at this rate, in the
   title's channel order (front left, front right, centre, LFE, then the surrounds); NULL when it
   cannot be opened (the reason in error). */
void *rt_audio_host_open(uint32_t channels, uint32_t rate, char *error, size_t error_size);

/* Queues frames; 0, or -1 on a host failure. */
int rt_audio_host_write(void *stream, const float *samples, uint32_t frames);

/* How many frames are queued and not yet played. */
uint32_t rt_audio_host_queued_frames(void *stream);

/* Closes a stream rt_audio_host_open gave; what it still had queued is dropped. */
void rt_audio_host_close(void *stream);

#endif
