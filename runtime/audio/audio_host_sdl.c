/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host sound through SDL3 (runtime/audio_host.h): one SDL audio stream per title port, bound to the
   default playback device. SDL converts the title's float frames to what the device takes and
   mixes the ports. The title's 8-channel float format (format 5) is the 7.1 order SDL expects,
   so no channel map is set (shadPS4's SDL backend passes it the same way). */
#include "runtime/audio_host.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static int enabled, sdl_audio_started;

int rt_audio_host_configure(int on)
{
    enabled = on;
    return 0;
}

int rt_audio_host_enabled(void) { return enabled; }

void *rt_audio_host_open(uint32_t channels, uint32_t rate, char *error, size_t error_size)
{
    if (!sdl_audio_started) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            snprintf(error, error_size, "SDL audio: %s", SDL_GetError());
            return NULL;
        }
        sdl_audio_started = 1;
    }
    const SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = (int)channels, .freq = (int)rate};
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        snprintf(error, error_size, "SDL audio stream: %s", SDL_GetError());
        return NULL;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return stream;
}

int rt_audio_host_write(void *stream, const float *samples, uint32_t frames)
{
    SDL_AudioSpec spec;
    if (!SDL_GetAudioStreamFormat(stream, &spec, NULL)) return -1;
    return SDL_PutAudioStreamData(stream, samples, (int)(frames * (uint32_t)spec.channels * sizeof(float))) ? 0 : -1;
}

void rt_audio_host_close(void *stream) { SDL_DestroyAudioStream(stream); }

uint32_t rt_audio_host_queued_frames(void *stream)
{
    SDL_AudioSpec spec;
    if (!SDL_GetAudioStreamFormat(stream, &spec, NULL)) return 0;
    const int queued = SDL_GetAudioStreamQueued(stream);
    return queued > 0 ? (uint32_t)queued / ((uint32_t)spec.channels * sizeof(float)) : 0;
}
