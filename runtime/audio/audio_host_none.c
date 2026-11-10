/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Builds without SDL3 have no host sound (runtime/audio_host.h). */
#include "runtime/audio_host.h"

#include <stdio.h>

int rt_audio_host_configure(int enabled) { return enabled ? -1 : 0; }
int rt_audio_host_enabled(void) { return 0; }

void *rt_audio_host_open(uint32_t channels, uint32_t rate, char *error, size_t error_size)
{
    (void)channels, (void)rate;
    snprintf(error, error_size, "this build has no host sound (SDL3 was not found)");
    return NULL;
}

int rt_audio_host_write(void *stream, const float *samples, uint32_t frames)
{
    (void)stream, (void)samples, (void)frames;
    return -1;
}

void rt_audio_host_close(void *stream) { (void)stream; }

uint32_t rt_audio_host_queued_frames(void *stream)
{
    (void)stream;
    return 0;
}
