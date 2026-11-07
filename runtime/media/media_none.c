/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Movie decoding when the runtime is built without FFmpeg (runtime/media_host.h): nothing can be
   opened, so the video player stops by name instead of playing. */
#include "runtime/media_host.h"

#include <stdio.h>

int rt_media_available(void) { return 0; }

rt_media *rt_media_open(const char *host_path, char *error, size_t error_size)
{
    (void)host_path;
    if (error && error_size) snprintf(error, error_size, "this build has no movie decoder (FFmpeg was not found)");
    return NULL;
}

void rt_media_close(rt_media *media) { (void)media; }
uint32_t rt_media_stream_count(const rt_media *media) { (void)media; return 0; }
const rt_media_stream *rt_media_stream_info(const rt_media *media, uint32_t index) { (void)media; (void)index; return NULL; }
int rt_media_select(rt_media *media, uint32_t index) { (void)media; (void)index; return -1; }

int rt_media_next_video(rt_media *media, uint8_t *nv12, uint32_t pitch, uint32_t rows, uint64_t *time_ms,
                        char *error, size_t error_size)
{
    (void)media; (void)nv12; (void)pitch; (void)rows; (void)time_ms; (void)error; (void)error_size;
    return -1;
}

int rt_media_next_audio(rt_media *media, int16_t *pcm, uint32_t max_frames, uint32_t *frames, uint64_t *time_ms,
                        char *error, size_t error_size)
{
    (void)media; (void)pcm; (void)max_frames; (void)frames; (void)time_ms; (void)error; (void)error_size;
    return -1;
}

int rt_media_rewind(rt_media *media) { (void)media; return -1; }
