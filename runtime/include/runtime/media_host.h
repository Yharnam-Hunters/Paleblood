/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Movie decoding on the host (runtime/media/): the files the title plays through its video player
   (runtime/syslib/avplayer.c), decoded with FFmpeg when the runtime is built with it. Without
   FFmpeg rt_media_available() is 0 and opening a movie fails, so the player stops by name.

   Every call here may use a lot of stack (FFmpeg probes and decodes on the calling thread), so
   callers run them on host threads, never on a guest thread's small stack. One rt_media is used by
   one thread at a time. */
#ifndef RUNTIME_MEDIA_HOST_H
#define RUNTIME_MEDIA_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef enum { RT_MEDIA_VIDEO, RT_MEDIA_AUDIO } rt_media_kind;

/* A stream the runtime can play: video or audio. Other streams are not listed. */
typedef struct {
    rt_media_kind kind;
    char language[4];          /* ISO 639 code from the file's metadata, NUL-terminated; "" if none */
    uint64_t duration_ms;
    uint32_t width, height;    /* video: the picture's size in pixels */
    float sample_aspect;       /* video: the file's sample aspect ratio, 0 when unknown */
    uint32_t channels, rate;   /* audio: channel count and samples per second */
} rt_media_stream;

typedef struct rt_media rt_media;

/* Whether this build can decode movies at all. */
int rt_media_available(void);

/* Opens and probes a movie file; NULL when it cannot be opened or has no playable stream (the
   reason in error). */
rt_media *rt_media_open(const char *host_path, char *error, size_t error_size);
void rt_media_close(rt_media *media);

uint32_t rt_media_stream_count(const rt_media *media);
const rt_media_stream *rt_media_stream_info(const rt_media *media, uint32_t index);

/* Chooses the one video stream and the one audio stream to decode (index from the list above);
   decoding begins at the first next_* call. 0, or -1 for an index that is not that kind. */
int rt_media_select(rt_media *media, uint32_t index);

/* The next picture of the selected video stream as NV12: luma rows of `pitch` bytes, `rows` of
   them (at least the picture height), then the interleaved chroma rows, rows / 2 of them, also
   `pitch` apart. 1 with its presentation time, 0 at the end of the stream, -1 on a decoding error
   (the reason in error). */
int rt_media_next_video(rt_media *media, uint8_t *nv12, uint32_t pitch, uint32_t rows, uint64_t *time_ms,
                        char *error, size_t error_size);

/* The next block of the selected audio stream as interleaved signed 16-bit samples, at most
   `max_frames` frames (a frame is one sample per channel). 1 with the frame count and presentation
   time, 0 at the end of the stream, -1 on a decoding error. */
int rt_media_next_audio(rt_media *media, int16_t *pcm, uint32_t max_frames, uint32_t *frames, uint64_t *time_ms,
                        char *error, size_t error_size);

/* Back to the start of the file, for looping. 0, or -1 when the file cannot seek. */
int rt_media_rewind(rt_media *media);

#endif
