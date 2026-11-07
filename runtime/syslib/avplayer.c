/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: video. The movie player (libSceAvPlayer): plays a movie file the title names,
   handing it pictures and sound as the movie's own clock reaches them.

   Facts used, from shadPS4's libSceAvPlayer (avplayer.h, avplayer_error.h, avplayer_state.cpp,
   avplayer_source.cpp) and the OpenOrbis toolchain's AvPlayer.h; the code is the runtime's own:
   - Init takes a 0x78-byte block: memory callbacks {object, allocate, deallocate, allocate texture,
     deallocate texture}, file callbacks {object, open, close, read at offset, size}, an event
     callback {object, function(object, event, source id, data)}, then the debug level, base
     priority, the number of output video buffers, auto start and the default language. It returns
     the player's handle, NULL on failure.
   - PostInit takes a 0x80-byte block of decoder settings (the demuxer's video buffer size and the
     video and audio decoder types and parameters) and returns 0.
   - Events: 1 stopped, 2 ready (the source's streams are known), 3 playing, 4 paused; the source id
     is 0 and the data NULL for these.
   - A stream's info is 0x28 bytes {type (0 video, 1 audio), reserved, details[16], duration in ms,
     start time}; audio details are {channels (16-bit), reserved, sample rate, size, language[4]},
     video details {width, height, aspect ratio (float), language[4]}.
   - Frames: {data, reserved[4], timestamp in ms, details}. A video frame (the Ex form, 0x68 bytes) is
     NV12 in a texture buffer from the title's allocator (alignment 0x100): luma rows `pitch` bytes
     apart, pitch the width rounded up to 64, height rounded up to 16, then the interleaved chroma;
     its details give width and height (rounded up to 16), the four crop offsets that bring them back
     to the picture, the pitch and 8-bit depths. An audio frame is at most 1024 interleaved signed
     16-bit sample frames; its details give channels, sample rate and size in bytes.
   - A picture is handed out only once the player's clock reaches its timestamp; errors are
     0x806A0001 (invalid parameters) and 0x806A0002 (operation failed).

   The executable's calls (one wrapper, 0x02cfdf33 to 0x02d004a9) pass memory and event callbacks
   but no file callbacks, auto start 0 and no default language; they enable one video and one audio
   stream (chosen by the title's language) after the ready event, read a picture's width, height and
   crops and derive the planes from the width as above, and read a sound frame's data, size,
   channels and rate. The runtime decodes on a host thread of its own (runtime/media_host.h, FFmpeg),
   a few frames ahead, so the guest's small stacks never run the decoder; the guest calls only copy
   finished frames into the title's buffers. The clock is the host's monotonic clock from Start, less
   the time paused; sound is handed out up to 0.1 s ahead of it to keep the title's mixer fed.
   Stops by name: file callbacks, auto start, a missing allocator or event callback, a movie the
   runtime cannot open or decode, and an unknown handle. */
#include "runtime/media_host.h"
#include "runtime/mounts.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AVPLAYER_ERROR_INVALID_PARAMS ((int32_t)0x806A0001u)

enum { EVENT_STOPPED = 1, EVENT_READY = 2, EVENT_PLAYING = 3, EVENT_PAUSED = 4 };
enum { STREAM_VIDEO = 0, STREAM_AUDIO = 1 };
enum { VIDEO_AHEAD = 3, AUDIO_AHEAD = 24, AUDIO_FRAME_SAMPLES = 1024, MAX_VIDEO_SLOTS = 8, AUDIO_SLOTS = 4 };
enum { AUDIO_LEAD_MS = 100, TEXTURE_ALIGN = 0x100, AUDIO_ALIGN = 0x40 };

typedef struct {
    void *object;
    void *(*allocate)(void *object, uint32_t align, uint32_t size);
    void (*deallocate)(void *object, void *memory);
    void *(*allocate_texture)(void *object, uint32_t align, uint32_t size);
    void (*deallocate_texture)(void *object, void *memory);
} memory_callbacks;

typedef struct {
    void *object;
    void *open, *close, *read_offset, *size;
} file_callbacks;

typedef struct {
    void *object;
    void (*event)(void *object, int32_t event, int32_t source, void *data);
} event_callback;

typedef struct {
    memory_callbacks memory;
    file_callbacks file;
    event_callback event;
    int32_t debug_level;
    uint32_t base_priority;
    int32_t output_video_buffers;
    uint8_t auto_start, reserved[3];
    const char *default_language;
} init_data;
_Static_assert(sizeof(init_data) == 0x78, "SceAvPlayerInitData is 0x78 bytes");

typedef struct {
    uint32_t type;
    uint8_t reserved[4];
    union {
        struct { uint16_t channels; uint8_t reserved[2]; uint32_t rate, size; char language[4]; } audio;
        struct { uint32_t width, height; float aspect; char language[4]; } video;
        uint8_t raw[16];
    } details;
    uint64_t duration_ms, start_time;
} stream_info;
_Static_assert(sizeof(stream_info) == 0x28, "SceAvPlayerStreamInfo is 0x28 bytes");

typedef struct {
    void *data;
    uint8_t reserved[4];
    uint64_t timestamp_ms;
    struct { uint16_t channels; uint8_t reserved[2]; uint32_t rate, size; char language[4]; } audio;
} audio_frame_info;
_Static_assert(sizeof(audio_frame_info) == 0x28, "SceAvPlayerFrameInfo is 0x28 bytes");

typedef struct {
    void *data;
    uint8_t reserved[4];
    uint64_t timestamp_ms;
    struct {
        uint32_t width, height;
        float aspect;
        char language[4];
        uint32_t framerate, crop_left, crop_right, crop_top, crop_bottom, pitch;
        uint8_t luma_bits, chroma_bits, full_range, reserved[37];
    } video;
} video_frame_info;
_Static_assert(sizeof(video_frame_info) == 0x68, "SceAvPlayerFrameInfoEx is 0x68 bytes");
_Static_assert(offsetof(video_frame_info, video.crop_left) == 0x2c, "crop offsets at 0x2c");

typedef struct { uint8_t *pixels; uint64_t time_ms; } decoded_picture;
typedef struct { int16_t *samples; uint32_t frames; uint64_t time_ms; } decoded_sound;

typedef struct {
    init_data init;
    pthread_mutex_t lock;
    pthread_cond_t wake;           /* the decoder thread waits on it for room or for the end */
    rt_media *media;
    int video, audio;              /* the enabled streams (listed index), -1 for none */
    uint32_t width, height, pitch, rows, channels, rate;
    size_t picture_bytes;
    uint8_t *video_slots[MAX_VIDEO_SLOTS];
    int16_t *audio_slots[AUDIO_SLOTS];
    int video_slot_count, next_video_slot, next_audio_slot;
    decoded_picture pictures[VIDEO_AHEAD];
    decoded_sound sounds[AUDIO_AHEAD];
    int picture_head, picture_count, sound_head, sound_count;
    int video_ended, audio_ended, looping, started, paused, stopped, stop_reported;
    uint64_t loop_offset_ms, paused_ms;
    struct timespec start, pause_start;
    pthread_t decoder;
    int decoder_running, quit;
    char error[256];
} player;

static pthread_mutex_t players_lock = PTHREAD_MUTEX_INITIALIZER;
static char path_buffer[4096]; /* guest stacks are small: the host path is built here, under players_lock */

static uint64_t elapsed_ms(const struct timespec *since)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec - since->tv_sec) * 1000 + (uint64_t)((now.tv_nsec - since->tv_nsec) / 1000000);
}

/* The movie's clock: milliseconds played since Start, standing still while paused. */
static uint64_t clock_ms(const player *p)
{
    if (!p->started) return 0;
    const uint64_t total = elapsed_ms(&p->start);
    const uint64_t paused = p->paused_ms + (p->paused ? elapsed_ms(&p->pause_start) : 0);
    return total > paused ? total - paused : 0;
}

static player *checked(const char *fn, void *handle)
{
    if (!handle) rt_fatal(fn, "no player: the error value for a null handle is not established");
    return handle;
}

static void send_event(player *p, int32_t event) { p->init.event.event(p->init.event.object, event, 0, NULL); }

/* Runs work(argument) on a host thread with an ordinary stack and waits for it: decoding and
   probing never run on a guest thread's small stack. */
static void on_host_thread(const char *fn, void *(*work)(void *), void *argument)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, work, argument)) rt_fatal(fn, "the host could not start a thread");
    pthread_join(thread, NULL);
}

static void *player_init(const init_data *data)
{
    static const char fn[] = "sceAvPlayerInit";
    if (!data) rt_fatal(fn, "no init data: the error value is not established");
    if (data->file.open || data->file.close || data->file.read_offset || data->file.size)
        rt_fatal(fn, "file callbacks: reading the movie through the title is not implemented");
    if (data->auto_start) rt_fatal(fn, "auto start: choosing streams by the default language is not implemented");
    if (!data->memory.allocate || !data->memory.deallocate || !data->memory.allocate_texture || !data->memory.deallocate_texture)
        rt_fatal(fn, "no memory callbacks: where the player's own memory would come from is not established");
    if (!data->event.event) rt_fatal(fn, "no event callback: playing without one is not implemented");
    player *p = calloc(1, sizeof *p);
    if (!p) rt_fatal(fn, "the host is out of memory");
    p->init = *data;
    p->video = p->audio = -1;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->wake, NULL);
    return p;
}

static int32_t player_post_init(void *handle, const void *data)
{
    static const char fn[] = "sceAvPlayerPostInit";
    checked(fn, handle);
    if (!data) return AVPLAYER_ERROR_INVALID_PARAMS;
    return 0; /* the decoder choices change only how the console decodes, not what comes out */
}

static void *open_movie(void *argument)
{
    player *p = argument;
    p->media = rt_media_open(path_buffer, p->error, sizeof p->error);
    return NULL;
}

static int32_t player_add_source(void *handle, const char *path)
{
    static const char fn[] = "sceAvPlayerAddSource";
    player *p = checked(fn, handle);
    if (!path) return AVPLAYER_ERROR_INVALID_PARAMS;
    if (p->media) rt_fatal(fn, "a second source: playlists are not implemented");
    pthread_mutex_lock(&players_lock);
    rt_host_path(fn, path, path_buffer, sizeof path_buffer);
    on_host_thread(fn, open_movie, p);
    pthread_mutex_unlock(&players_lock);
    if (!p->media) rt_fatal(fn, p->error);
    send_event(p, EVENT_READY);
    return 0;
}

static int32_t player_stream_count(void *handle)
{
    player *p = checked("sceAvPlayerStreamCount", handle);
    return p->media ? (int32_t)rt_media_stream_count(p->media) : 0;
}

static int32_t player_stream_info(void *handle, uint32_t index, stream_info *info)
{
    static const char fn[] = "sceAvPlayerGetStreamInfo";
    player *p = checked(fn, handle);
    const rt_media_stream *s = p->media ? rt_media_stream_info(p->media, index) : NULL;
    if (!s || !info) return AVPLAYER_ERROR_INVALID_PARAMS;
    memset(info, 0, sizeof *info);
    info->duration_ms = s->duration_ms;
    if (s->kind == RT_MEDIA_VIDEO) {
        info->type = STREAM_VIDEO;
        info->details.video.width = s->width;
        info->details.video.height = s->height;
        info->details.video.aspect = s->sample_aspect;
        memcpy(info->details.video.language, s->language, sizeof s->language);
    } else {
        info->type = STREAM_AUDIO;
        info->details.audio.channels = (uint16_t)s->channels;
        info->details.audio.rate = s->rate;
        memcpy(info->details.audio.language, s->language, sizeof s->language);
    }
    return 0;
}

typedef struct { player *p; uint32_t index; int result; } select_request;

static void *select_stream(void *argument)
{
    select_request *r = argument;
    r->result = rt_media_select(r->p->media, r->index);
    return NULL;
}

static int32_t player_enable_stream(void *handle, uint32_t index)
{
    static const char fn[] = "sceAvPlayerEnableStream";
    player *p = checked(fn, handle);
    const rt_media_stream *s = p->media ? rt_media_stream_info(p->media, index) : NULL;
    if (!s) return AVPLAYER_ERROR_INVALID_PARAMS;
    if (p->started) rt_fatal(fn, "enabling a stream while playing is not implemented");
    select_request request = {p, index, 0};
    on_host_thread(fn, select_stream, &request);
    if (request.result < 0) rt_fatal(fn, "the host decoder could not open the stream");
    if (s->kind == RT_MEDIA_VIDEO) {
        p->video = (int)index;
        p->width = (s->width + 15) & ~15u;
        p->height = (s->height + 15) & ~15u;
        p->pitch = (s->width + 63) & ~63u;
        p->rows = p->height;
        p->picture_bytes = (size_t)p->pitch * p->rows * 3 / 2;
    } else {
        p->audio = (int)index;
        p->channels = s->channels;
        p->rate = s->rate;
    }
    return 0;
}

static int32_t player_set_looping(void *handle, int looping)
{
    player *p = checked("sceAvPlayerSetLooping", handle);
    pthread_mutex_lock(&p->lock);
    p->looping = looping != 0;
    pthread_mutex_unlock(&p->lock);
    return 0;
}

/* Whether the decoder thread has nothing to do: both queues full or their streams ended. */
static int decoder_idle(const player *p)
{
    const int video_done = p->video < 0 || p->video_ended || p->picture_count == VIDEO_AHEAD;
    const int audio_done = p->audio < 0 || p->audio_ended || p->sound_count == AUDIO_AHEAD;
    return video_done && audio_done;
}

/* At the end of every enabled stream of a looping movie: back to the start, the clock carried on. */
static void loop_if_due(player *p)
{
    const int ended = (p->video < 0 || p->video_ended) && (p->audio < 0 || p->audio_ended);
    if (!p->looping || !ended) return;
    if (rt_media_rewind(p->media) < 0) return;
    const rt_media_stream *s = rt_media_stream_info(p->media, (uint32_t)(p->video >= 0 ? p->video : p->audio));
    p->loop_offset_ms += s->duration_ms;
    p->video_ended = p->audio_ended = 0;
}

static void decode_picture(player *p)
{
    pthread_mutex_unlock(&p->lock);
    uint8_t *pixels = malloc(p->picture_bytes);
    uint64_t time_ms = 0;
    const int got = pixels ? rt_media_next_video(p->media, pixels, p->pitch, p->rows, &time_ms, p->error, sizeof p->error) : -1;
    pthread_mutex_lock(&p->lock);
    if (got <= 0) {
        free(pixels);
        p->video_ended = got == 0 ? 1 : -1; /* -1: an error, reported to the next guest call */
        return;
    }
    p->pictures[(p->picture_head + p->picture_count++) % VIDEO_AHEAD] = (decoded_picture){pixels, time_ms + p->loop_offset_ms};
}

static void decode_sound(player *p)
{
    pthread_mutex_unlock(&p->lock);
    int16_t *samples = malloc((size_t)AUDIO_FRAME_SAMPLES * p->channels * sizeof *samples);
    uint32_t frames = 0;
    uint64_t time_ms = 0;
    const int got = samples ? rt_media_next_audio(p->media, samples, AUDIO_FRAME_SAMPLES, &frames, &time_ms, p->error, sizeof p->error) : -1;
    pthread_mutex_lock(&p->lock);
    if (got <= 0) {
        free(samples);
        p->audio_ended = got == 0 ? 1 : -1;
        return;
    }
    p->sounds[(p->sound_head + p->sound_count++) % AUDIO_AHEAD] = (decoded_sound){samples, frames, time_ms + p->loop_offset_ms};
}

/* The decoder thread: keeps a few pictures and sound blocks ready ahead of the clock. */
static void *decoder_thread(void *argument)
{
    player *p = argument;
    pthread_mutex_lock(&p->lock);
    while (!p->quit) {
        loop_if_due(p);
        if (decoder_idle(p)) {
            pthread_cond_wait(&p->wake, &p->lock);
            continue;
        }
        const int video_wants = p->video >= 0 && !p->video_ended && p->picture_count < VIDEO_AHEAD;
        const int audio_wants = p->audio >= 0 && !p->audio_ended && p->sound_count < AUDIO_AHEAD;
        /* the one further behind goes first, so the shared demuxer reads in file order */
        const uint64_t video_at = p->picture_count ? p->pictures[(p->picture_head + p->picture_count - 1) % VIDEO_AHEAD].time_ms : 0;
        const uint64_t audio_at = p->sound_count ? p->sounds[(p->sound_head + p->sound_count - 1) % AUDIO_AHEAD].time_ms : 0;
        if (video_wants && (!audio_wants || video_at <= audio_at)) decode_picture(p);
        else decode_sound(p);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

static void allocate_buffers(const char *fn, player *p)
{
    const memory_callbacks *m = &p->init.memory;
    if (p->video >= 0) {
        int slots = p->init.output_video_buffers > 2 ? p->init.output_video_buffers : 2;
        if (slots > MAX_VIDEO_SLOTS) slots = MAX_VIDEO_SLOTS;
        for (int i = 0; i < slots; i++)
            if (!(p->video_slots[i] = m->allocate_texture(m->object, TEXTURE_ALIGN, (uint32_t)p->picture_bytes)))
                rt_fatal(fn, "the title's allocator gave no memory for a picture");
        p->video_slot_count = slots;
    }
    if (p->audio >= 0) {
        const uint32_t bytes = AUDIO_FRAME_SAMPLES * p->channels * (uint32_t)sizeof(int16_t);
        for (int i = 0; i < AUDIO_SLOTS; i++)
            if (!(p->audio_slots[i] = m->allocate(m->object, AUDIO_ALIGN, bytes)))
                rt_fatal(fn, "the title's allocator gave no memory for sound");
    }
}

static int32_t player_start(void *handle)
{
    static const char fn[] = "sceAvPlayerStart";
    player *p = checked(fn, handle);
    if (!p->media) rt_fatal(fn, "no source added: the error value is not established");
    if (p->started) rt_fatal(fn, "a second start: the answer is not established");
    if (p->video < 0 && p->audio < 0) rt_fatal(fn, "no stream enabled: which ones the player would choose is not established");
    allocate_buffers(fn, p);
    pthread_mutex_lock(&p->lock);
    clock_gettime(CLOCK_MONOTONIC, &p->start);
    p->started = 1;
    if (pthread_create(&p->decoder, NULL, decoder_thread, p)) rt_fatal(fn, "the host could not start the decoder thread");
    p->decoder_running = 1;
    pthread_mutex_unlock(&p->lock);
    send_event(p, EVENT_PLAYING);
    return 0;
}

static void stop_decoder(player *p)
{
    pthread_mutex_lock(&p->lock);
    p->quit = 1;
    pthread_cond_signal(&p->wake);
    pthread_mutex_unlock(&p->lock);
    if (p->decoder_running) pthread_join(p->decoder, NULL);
    p->decoder_running = 0;
}

static int32_t player_stop(void *handle)
{
    player *p = checked("sceAvPlayerStop", handle);
    stop_decoder(p);
    p->stopped = 1;
    if (!p->stop_reported) {
        p->stop_reported = 1;
        send_event(p, EVENT_STOPPED);
    }
    return 0;
}

/* Reports a decoding error as a stop by name, and the end of a movie that does not loop as the
   stopped event, once. Called with the lock held; the event is sent after it is released. */
static int ended(const char *fn, player *p)
{
    if (p->video_ended < 0 || p->audio_ended < 0) rt_fatal(fn, p->error);
    const int video_done = p->video < 0 || (p->video_ended && !p->picture_count);
    const int audio_done = p->audio < 0 || (p->audio_ended && !p->sound_count);
    return p->started && !p->looping && video_done && audio_done;
}

static int player_is_active(void *handle)
{
    static const char fn[] = "sceAvPlayerIsActive";
    player *p = checked(fn, handle);
    if (!p->media || p->stopped) return 0;
    pthread_mutex_lock(&p->lock);
    const int over = ended(fn, p);
    pthread_mutex_unlock(&p->lock);
    if (!over) return 1;
    if (!p->stop_reported) {
        p->stop_reported = 1;
        send_event(p, EVENT_STOPPED);
    }
    return 0;
}

static uint64_t player_current_time(void *handle)
{
    player *p = checked("sceAvPlayerCurrentTime", handle);
    pthread_mutex_lock(&p->lock);
    const uint64_t now = clock_ms(p);
    pthread_mutex_unlock(&p->lock);
    return now;
}

static int32_t player_pause(void *handle)
{
    player *p = checked("sceAvPlayerPause", handle);
    pthread_mutex_lock(&p->lock);
    const int changed = p->started && !p->paused;
    if (changed) {
        clock_gettime(CLOCK_MONOTONIC, &p->pause_start);
        p->paused = 1;
    }
    pthread_mutex_unlock(&p->lock);
    if (changed) send_event(p, EVENT_PAUSED);
    return 0;
}

static int32_t player_resume(void *handle)
{
    player *p = checked("sceAvPlayerResume", handle);
    pthread_mutex_lock(&p->lock);
    const int changed = p->paused;
    if (changed) {
        p->paused_ms += elapsed_ms(&p->pause_start);
        p->paused = 0;
    }
    pthread_mutex_unlock(&p->lock);
    if (changed) send_event(p, EVENT_PLAYING);
    return 0;
}

static int player_video_data(void *handle, video_frame_info *info)
{
    static const char fn[] = "sceAvPlayerGetVideoDataEx";
    player *p = checked(fn, handle);
    if (!info || p->video < 0 || !p->started || p->stopped) return 0;
    pthread_mutex_lock(&p->lock);
    ended(fn, p);
    const decoded_picture *next = &p->pictures[p->picture_head];
    if (p->paused || !p->picture_count || next->time_ms > clock_ms(p)) {
        pthread_mutex_unlock(&p->lock);
        return 0;
    }
    uint8_t *slot = p->video_slots[p->next_video_slot];
    p->next_video_slot = (p->next_video_slot + 1) % p->video_slot_count;
    memcpy(slot, next->pixels, p->picture_bytes);
    const rt_media_stream *s = rt_media_stream_info(p->media, (uint32_t)p->video);
    memset(info, 0, sizeof *info);
    info->data = slot;
    info->timestamp_ms = next->time_ms;
    info->video.width = p->width;
    info->video.height = p->height;
    info->video.aspect = s->sample_aspect;
    memcpy(info->video.language, s->language, sizeof s->language);
    info->video.crop_right = p->width - s->width;
    info->video.crop_bottom = p->height - s->height;
    info->video.pitch = p->pitch;
    info->video.luma_bits = info->video.chroma_bits = 8;
    free(next->pixels);
    p->picture_head = (p->picture_head + 1) % VIDEO_AHEAD;
    p->picture_count--;
    pthread_cond_signal(&p->wake);
    pthread_mutex_unlock(&p->lock);
    return 1;
}

static int player_audio_data(void *handle, audio_frame_info *info)
{
    static const char fn[] = "sceAvPlayerGetAudioData";
    player *p = checked(fn, handle);
    if (!info || p->audio < 0 || !p->started || p->stopped) return 0;
    pthread_mutex_lock(&p->lock);
    ended(fn, p);
    const decoded_sound *next = &p->sounds[p->sound_head];
    if (p->paused || !p->sound_count || next->time_ms > clock_ms(p) + AUDIO_LEAD_MS) {
        pthread_mutex_unlock(&p->lock);
        return 0;
    }
    int16_t *slot = p->audio_slots[p->next_audio_slot];
    p->next_audio_slot = (p->next_audio_slot + 1) % AUDIO_SLOTS;
    const uint32_t bytes = next->frames * p->channels * (uint32_t)sizeof(int16_t);
    memcpy(slot, next->samples, bytes);
    memset(info, 0, sizeof *info);
    info->data = slot;
    info->timestamp_ms = next->time_ms;
    info->audio.channels = (uint16_t)p->channels;
    info->audio.rate = p->rate;
    info->audio.size = bytes;
    free(next->samples);
    p->sound_head = (p->sound_head + 1) % AUDIO_AHEAD;
    p->sound_count--;
    pthread_cond_signal(&p->wake);
    pthread_mutex_unlock(&p->lock);
    return 1;
}

static void *close_movie(void *argument)
{
    player *p = argument;
    rt_media_close(p->media);
    p->media = NULL;
    return NULL;
}

static int32_t player_close(void *handle)
{
    static const char fn[] = "sceAvPlayerClose";
    player *p = checked(fn, handle);
    stop_decoder(p);
    on_host_thread(fn, close_movie, p);
    const memory_callbacks *m = &p->init.memory;
    for (int i = 0; i < p->video_slot_count; i++) m->deallocate_texture(m->object, p->video_slots[i]);
    for (int i = 0; i < AUDIO_SLOTS; i++)
        if (p->audio_slots[i]) m->deallocate(m->object, p->audio_slots[i]);
    for (int i = 0; i < p->picture_count; i++) free(p->pictures[(p->picture_head + i) % VIDEO_AHEAD].pixels);
    for (int i = 0; i < p->sound_count; i++) free(p->sounds[(p->sound_head + i) % AUDIO_AHEAD].samples);
    pthread_cond_destroy(&p->wake);
    pthread_mutex_destroy(&p->lock);
    free(p);
    return 0;
}

RT_SYSLIB("libSceAvPlayer", sceAvPlayerInit, player_init);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerPostInit, player_post_init);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerAddSource, player_add_source);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerStreamCount, player_stream_count);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerGetStreamInfo, player_stream_info);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerEnableStream, player_enable_stream);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerSetLooping, player_set_looping);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerStart, player_start);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerStop, player_stop);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerIsActive, player_is_active);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerCurrentTime, player_current_time);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerPause, player_pause);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerResume, player_resume);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerGetVideoDataEx, player_video_data);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerGetAudioData, player_audio_data);
RT_SYSLIB("libSceAvPlayer", sceAvPlayerClose, player_close);
