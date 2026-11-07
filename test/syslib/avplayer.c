/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceAvPlayer (runtime/syslib/avplayer.c): a short movie the test encodes itself (72x40 MPEG-4
   at 25 frames a second, each frame one flat grey, and a stereo AAC tone) played through the
   title's calls: events, stream info, NV12 pictures with their crops and pitch, timing, sound,
   pause, the end of the movie, looping, and the stops by name. Without FFmpeg only the stops are
   checked. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef PB_TEST_MEDIA
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#endif

enum { MOVIE_WIDTH = 72, MOVIE_HEIGHT = 40, MOVIE_FRAMES = 12, MOVIE_FPS = 25, TONE_RATE = 44100 };

typedef struct {
    void *object;
    void *(*allocate)(void *, uint32_t, uint32_t);
    void (*deallocate)(void *, void *);
    void *(*allocate_texture)(void *, uint32_t, uint32_t);
    void (*deallocate_texture)(void *, void *);
    void *file[5];
    void *event_object;
    void (*event)(void *, int32_t, int32_t, void *);
    int32_t debug_level;
    uint32_t base_priority;
    int32_t output_video_buffers;
    uint8_t auto_start, reserved[3];
    const char *default_language;
} av_init;

typedef struct {
    uint32_t type;
    uint8_t reserved[4];
    union {
        struct { uint16_t channels; uint8_t r[2]; uint32_t rate, size; char language[4]; } audio;
        struct { uint32_t width, height; float aspect; char language[4]; } video;
    } details;
    uint64_t duration_ms, start_time;
} av_stream_info;

typedef struct {
    uint8_t *data;
    uint8_t reserved[4];
    uint64_t timestamp_ms;
    uint32_t width, height;
    float aspect;
    char language[4];
    uint32_t framerate, crop_left, crop_right, crop_top, crop_bottom, pitch;
    uint8_t luma_bits, chroma_bits, full_range, reserved1[37];
} av_video_frame;

typedef struct {
    int16_t *data;
    uint8_t reserved[4];
    uint64_t timestamp_ms;
    uint16_t channels;
    uint8_t r[2];
    uint32_t rate, size;
    char language[4];
} av_audio_frame;

typedef void *(*f_av_init)(const av_init *);
typedef int32_t (*f_av_handle)(void *);
typedef int32_t (*f_av_post_init)(void *, const void *);
typedef int32_t (*f_av_add)(void *, const char *);
typedef int32_t (*f_av_info)(void *, uint32_t, av_stream_info *);
typedef int32_t (*f_av_index)(void *, uint32_t);
typedef int32_t (*f_av_looping)(void *, int);
typedef int (*f_av_active)(void *);
typedef uint64_t (*f_av_time)(void *);
typedef int (*f_av_video)(void *, av_video_frame *);
typedef int (*f_av_audio)(void *, av_audio_frame *);

static int allocations, texture_allocations, last_texture_align;
static uint32_t last_texture_size;
static int events[16], event_count;

static void *test_allocate(void *object, uint32_t align, uint32_t size)
{
    (void)object;
    allocations++;
    return aligned_alloc(align, (size + align - 1) / align * align);
}

static void test_deallocate(void *object, void *memory) { (void)object; allocations--; free(memory); }

static void *test_allocate_texture(void *object, uint32_t align, uint32_t size)
{
    texture_allocations++;
    last_texture_align = (int)align;
    last_texture_size = size;
    return test_allocate(object, align, size);
}

static void test_deallocate_texture(void *object, void *memory) { texture_allocations--; test_deallocate(object, memory); }

static void test_event(void *object, int32_t event, int32_t source, void *data)
{
    (void)object;
    CHECK(source == 0 && data == NULL);
    if (event_count < 16) events[event_count++] = event;
}

static av_init test_init_data(void)
{
    av_init init = {0};
    init.allocate = test_allocate;
    init.deallocate = test_deallocate;
    init.allocate_texture = test_allocate_texture;
    init.deallocate_texture = test_deallocate_texture;
    init.event = test_event;
    init.output_video_buffers = 2;
    return init;
}

static char app0[512];

static void make_app0(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(app0, sizeof app0, "%s/pb-avplayer-XXXXXX", tmp ? tmp : "/tmp");
    CHECK(mkdtemp(app0) != NULL);
    char movie_dir[600];
    snprintf(movie_dir, sizeof movie_dir, "%s/movie", app0);
    CHECK(mkdir(movie_dir, 0700) == 0);
    rt_process.app0_dir = app0;
}

/* Stops by name: file callbacks, and a source the runtime cannot open. */
static void init_with_file_callbacks(void)
{
    av_init init = test_init_data();
    init.file[1] = (void *)test_allocate;
    ((f_av_init)fn("libSceAvPlayer", "sceAvPlayerInit"))(&init);
}

static void add_missing_source(void)
{
    av_init init = test_init_data();
    void *player = ((f_av_init)fn("libSceAvPlayer", "sceAvPlayerInit"))(&init);
    ((f_av_add)fn("libSceAvPlayer", "sceAvPlayerAddSource"))(player, "/app0/movie/none.mp4");
}

#ifdef PB_TEST_MEDIA
static AVStream *add_stream(AVFormatContext *out, AVCodecContext *codec)
{
    if (out->oformat->flags & AVFMT_GLOBALHEADER) codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(codec, codec->codec, NULL) < 0) return NULL;
    AVStream *stream = avformat_new_stream(out, NULL);
    if (!stream || avcodec_parameters_from_context(stream->codecpar, codec) < 0) return NULL;
    stream->time_base = codec->time_base;
    return stream;
}

static int write_frames(AVFormatContext *out, AVCodecContext *codec, AVStream *stream, AVFrame *frame)
{
    if (avcodec_send_frame(codec, frame) < 0) return -1;
    AVPacket *packet = av_packet_alloc();
    while (avcodec_receive_packet(codec, packet) == 0) {
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        if (av_interleaved_write_frame(out, packet) < 0) return av_packet_free(&packet), -1;
    }
    av_packet_free(&packet);
    return 0;
}

/* Frame i of the picture is one flat grey, luma 16 + 16 i; the tone is 440 Hz. */
static int encode_movie(const char *path)
{
    AVFormatContext *out = NULL;
    if (avformat_alloc_output_context2(&out, NULL, "mp4", path) < 0) return -1;
    AVCodecContext *video = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_MPEG4));
    video->width = MOVIE_WIDTH;
    video->height = MOVIE_HEIGHT;
    video->time_base = (AVRational){1, MOVIE_FPS};
    video->pix_fmt = AV_PIX_FMT_YUV420P;
    video->flags |= AV_CODEC_FLAG_QSCALE;
    video->global_quality = FF_QP2LAMBDA * 2;
    AVCodecContext *audio = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_AAC));
    audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio->sample_rate = TONE_RATE;
    audio->time_base = (AVRational){1, TONE_RATE};
    audio->bit_rate = 64000;
    av_channel_layout_default(&audio->ch_layout, 2);
    AVStream *video_stream = add_stream(out, video), *audio_stream = add_stream(out, audio);
    int ok = video_stream && audio_stream && avio_open(&out->pb, path, AVIO_FLAG_WRITE) >= 0 &&
             avformat_write_header(out, NULL) >= 0;
    AVFrame *frame = av_frame_alloc();
    for (int i = 0; ok && i < MOVIE_FRAMES; i++) {
        frame->format = video->pix_fmt;
        frame->width = video->width;
        frame->height = video->height;
        ok = av_frame_get_buffer(frame, 0) >= 0;
        for (int plane = 0; ok && plane < 3; plane++)
            memset(frame->data[plane], plane ? 128 : 16 + 16 * i, (size_t)frame->linesize[plane] * (plane ? MOVIE_HEIGHT / 2 : MOVIE_HEIGHT));
        frame->pts = i;
        ok = ok && write_frames(out, video, video_stream, frame) == 0;
        av_frame_unref(frame);
    }
    const int tone_frames = TONE_RATE * MOVIE_FRAMES / MOVIE_FPS / audio->frame_size + 1;
    for (int i = 0; ok && i < tone_frames; i++) {
        frame->format = audio->sample_fmt;
        frame->nb_samples = audio->frame_size;
        frame->sample_rate = audio->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &audio->ch_layout);
        ok = av_frame_get_buffer(frame, 0) >= 0;
        for (int c = 0; ok && c < 2; c++)
            for (int s = 0; s < audio->frame_size; s++)
                ((float *)frame->data[c])[s] = 0.25f * (float)((i * audio->frame_size + s) / 50 % 2 ? 1 : -1);
        frame->pts = (int64_t)i * audio->frame_size;
        ok = ok && write_frames(out, audio, audio_stream, frame) == 0;
        av_frame_unref(frame);
    }
    ok = ok && write_frames(out, video, video_stream, NULL) == 0 && write_frames(out, audio, audio_stream, NULL) == 0 &&
         av_write_trailer(out) >= 0;
    av_frame_free(&frame);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    if (out->pb) avio_closep(&out->pb);
    avformat_free_context(out);
    return ok ? 0 : -1;
}

static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

static void play_movie(void)
{
    const f_av_init init_fn = (f_av_init)fn("libSceAvPlayer", "sceAvPlayerInit");
    const f_av_post_init post_init = (f_av_post_init)fn("libSceAvPlayer", "sceAvPlayerPostInit");
    const f_av_add add = (f_av_add)fn("libSceAvPlayer", "sceAvPlayerAddSource");
    const f_av_handle count = (f_av_handle)fn("libSceAvPlayer", "sceAvPlayerStreamCount");
    const f_av_info info = (f_av_info)fn("libSceAvPlayer", "sceAvPlayerGetStreamInfo");
    const f_av_index enable = (f_av_index)fn("libSceAvPlayer", "sceAvPlayerEnableStream");
    const f_av_handle start = (f_av_handle)fn("libSceAvPlayer", "sceAvPlayerStart");
    const f_av_handle pause = (f_av_handle)fn("libSceAvPlayer", "sceAvPlayerPause");
    const f_av_handle resume = (f_av_handle)fn("libSceAvPlayer", "sceAvPlayerResume");
    const f_av_active active = (f_av_active)fn("libSceAvPlayer", "sceAvPlayerIsActive");
    const f_av_time now = (f_av_time)fn("libSceAvPlayer", "sceAvPlayerCurrentTime");
    const f_av_video video = (f_av_video)fn("libSceAvPlayer", "sceAvPlayerGetVideoDataEx");
    const f_av_audio audio = (f_av_audio)fn("libSceAvPlayer", "sceAvPlayerGetAudioData");
    const f_av_handle close = (f_av_handle)fn("libSceAvPlayer", "sceAvPlayerClose");
    if (failures) return;

    av_init init = test_init_data();
    event_count = 0;
    void *player = init_fn(&init);
    CHECK(player != NULL);
    uint8_t post[0x80] = {0};
    CHECK(post_init(player, post) == 0);
    CHECK(add(player, NULL) == (int32_t)0x806A0001u);
    CHECK(add(player, "/app0/movie/test.mp4") == 0 && event_count == 1 && events[0] == 2); /* ready */
    CHECK(count(player) == 2);
    int video_index = -1, audio_index = -1;
    for (uint32_t i = 0; i < 2; i++) {
        av_stream_info s;
        CHECK(info(player, i, &s) == 0);
        if (s.type == 0) {
            video_index = (int)i;
            CHECK(s.details.video.width == MOVIE_WIDTH && s.details.video.height == MOVIE_HEIGHT);
        } else if (s.type == 1) {
            audio_index = (int)i;
            CHECK(s.details.audio.channels == 2 && s.details.audio.rate == TONE_RATE);
        }
        CHECK(s.duration_ms >= 400 && s.duration_ms <= 600);
    }
    CHECK(info(player, 2, &(av_stream_info){0}) == (int32_t)0x806A0001u);
    CHECK(video_index >= 0 && audio_index >= 0);
    CHECK(enable(player, (uint32_t)video_index) == 0 && enable(player, (uint32_t)audio_index) == 0);
    CHECK(now(player) == 0);
    CHECK(start(player) == 0 && event_count == 2 && events[1] == 3); /* playing */
    /* pitch 128 (72 up to 64), rows 48 (40 up to 16), NV12: 128 * 48 * 3 / 2 */
    CHECK(last_texture_align == 0x100 && last_texture_size == 128 * 48 * 3 / 2 && texture_allocations == 2);

    av_video_frame first = {0};
    for (int tries = 0; tries < 200 && !video(player, &first); tries++) sleep_ms(5);
    CHECK(first.data != NULL && first.timestamp_ms == 0);
    CHECK(first.width == 80 && first.height == 48 && first.pitch == 128);
    CHECK(first.crop_left == 0 && first.crop_top == 0 && first.crop_right == 8 && first.crop_bottom == 8);
    CHECK(first.luma_bits == 8 && first.chroma_bits == 8);
    if (first.data) {
        CHECK(abs(first.data[10 * 128 + 10] - 16) <= 6);                 /* frame 0's grey */
        CHECK(abs(first.data[128 * 48 + 4 * 128 + 10] - 128) <= 6);      /* chroma after pitch * rows */
    }
    av_video_frame second = {0};
    CHECK(video(player, &second) == 0); /* the next picture is 40 ms later: not yet */
    av_audio_frame sound = {0};
    for (int tries = 0; tries < 200 && !audio(player, &sound); tries++) sleep_ms(5);
    CHECK(sound.data != NULL && sound.channels == 2 && sound.rate == TONE_RATE);
    CHECK(sound.size > 0 && sound.size <= 1024 * 2 * 2 && sound.size % 4 == 0);

    CHECK(pause(player) == 0 && events[event_count - 1] == 4);
    const uint64_t paused_at = now(player);
    sleep_ms(60);
    CHECK(now(player) == paused_at && video(player, &second) == 0);
    CHECK(resume(player) == 0 && events[event_count - 1] == 3);

    uint64_t last = 0;
    int pictures = 1;
    for (int tries = 0; tries < 400 && active(player); tries++) {
        av_video_frame f = {0};
        if (video(player, &f)) {
            CHECK(f.timestamp_ms > last && f.data != NULL);
            if (f.data) CHECK(abs(f.data[10 * 128 + 10] - (16 + 16 * pictures)) <= 6);
            last = f.timestamp_ms;
            pictures++;
        }
        while (audio(player, &sound)) {}
        sleep_ms(5);
    }
    /* the MP4 muxer's edit list may hide the last frame (FFmpeg's own ffprobe decodes 11 of 12) */
    CHECK(pictures >= MOVIE_FRAMES - 1 && pictures <= MOVIE_FRAMES && last == (uint64_t)(pictures - 1) * 1000 / MOVIE_FPS);
    CHECK(!active(player) && events[event_count - 1] == 1); /* stopped, once */
    const int stops = event_count;
    CHECK(!active(player) && event_count == stops);
    CHECK(close(player) == 0 && allocations == 0 && texture_allocations == 0);
}

static void play_looping(void)
{
    av_init init = test_init_data();
    void *player = ((f_av_init)fn("libSceAvPlayer", "sceAvPlayerInit"))(&init);
    CHECK(((f_av_add)fn("libSceAvPlayer", "sceAvPlayerAddSource"))(player, "/app0/movie/test.mp4") == 0);
    av_stream_info s;
    uint32_t video_index = 0;
    for (uint32_t i = 0; i < 2; i++)
        if (((f_av_info)fn("libSceAvPlayer", "sceAvPlayerGetStreamInfo"))(player, i, &s) == 0 && s.type == 0) video_index = i;
    CHECK(((f_av_index)fn("libSceAvPlayer", "sceAvPlayerEnableStream"))(player, video_index) == 0);
    CHECK(((f_av_looping)fn("libSceAvPlayer", "sceAvPlayerSetLooping"))(player, 1) == 0);
    CHECK(((f_av_handle)fn("libSceAvPlayer", "sceAvPlayerStart"))(player) == 0);
    const f_av_video video = (f_av_video)fn("libSceAvPlayer", "sceAvPlayerGetVideoDataEx");
    uint64_t last = 0;
    int pictures = 0;
    for (int tries = 0; tries < 400 && pictures < MOVIE_FRAMES + 3; tries++) {
        av_video_frame f = {0};
        if (video(player, &f)) {
            CHECK(pictures == 0 || f.timestamp_ms > last);
            last = f.timestamp_ms;
            pictures++;
        }
        sleep_ms(5);
    }
    CHECK(pictures == MOVIE_FRAMES + 3 && last >= (uint64_t)(MOVIE_FRAMES - 1) * 1000 / MOVIE_FPS); /* past the end: looped */
    CHECK(((f_av_active)fn("libSceAvPlayer", "sceAvPlayerIsActive"))(player));
    CHECK(((f_av_handle)fn("libSceAvPlayer", "sceAvPlayerClose"))(player) == 0 && allocations == 0);
}
#endif

SYSLIB_TEST(avplayer)
{
    make_app0();
    CHECK(died_in_fatal(init_with_file_callbacks));
    CHECK(died_in_fatal(add_missing_source)); /* no such file, or no decoder in this build */
#ifdef PB_TEST_MEDIA
    char movie[700];
    snprintf(movie, sizeof movie, "%s/movie/test.mp4", app0);
    CHECK(encode_movie(movie) == 0);
    if (!failures) play_movie();
    if (!failures) play_looping();
    remove(movie);
#endif
    char movie_dir[600];
    snprintf(movie_dir, sizeof movie_dir, "%s/movie", app0);
    rmdir(movie_dir);
    rmdir(app0);
    rt_process.app0_dir = NULL;
}
