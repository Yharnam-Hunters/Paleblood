/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Movie decoding with FFmpeg (runtime/media_host.h): one demuxer per file, a decoder for the
   selected video stream and one for the selected audio stream. Packets read for the stream that
   is not asking yet wait in a small queue of their own, so the two decoders can be pulled at
   their own pace. Pictures come out as NV12 (libswscale), sound as interleaved signed 16-bit
   samples in the file's own channel order (libswresample). */
#include "runtime/media_host.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_STREAMS = 32 };

typedef struct queued_packet {
    AVPacket *packet;
    struct queued_packet *next;
} queued_packet;

typedef struct {
    int stream;                 /* FFmpeg stream index, -1 when none is selected */
    AVCodecContext *codec;
    queued_packet *head, *tail; /* packets read while the other decoder was asking */
    int draining;               /* the demuxer ended and the decoder was told so */
} decoder;

struct rt_media {
    AVFormatContext *format;
    rt_media_stream streams[MAX_STREAMS];
    int format_index[MAX_STREAMS]; /* the FFmpeg stream behind each listed stream */
    uint32_t count;
    decoder video, audio;
    AVFrame *frame;
    struct SwsContext *scaler;
    SwrContext *resampler;
    int16_t *pending;              /* converted samples not handed out yet */
    uint32_t pending_frames, pending_offset, pending_capacity;
    uint64_t pending_time_ms;
    int demuxer_ended;
};

int rt_media_available(void) { return 1; }

static void say(char *error, size_t size, const char *format, ...)
{
    if (!error || !size) return;
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static uint64_t to_ms(int64_t time, AVRational base)
{
    if (time == AV_NOPTS_VALUE || time < 0) return 0;
    return (uint64_t)av_rescale_q(time, base, (AVRational){1, 1000});
}

static void list_stream(rt_media *media, int index)
{
    const AVStream *stream = media->format->streams[index];
    const AVCodecParameters *p = stream->codecpar;
    if (p->codec_type != AVMEDIA_TYPE_VIDEO && p->codec_type != AVMEDIA_TYPE_AUDIO) return;
    if (media->count == MAX_STREAMS || !avcodec_find_decoder(p->codec_id)) return;
    rt_media_stream *s = &media->streams[media->count];
    memset(s, 0, sizeof *s);
    s->kind = p->codec_type == AVMEDIA_TYPE_VIDEO ? RT_MEDIA_VIDEO : RT_MEDIA_AUDIO;
    const AVDictionaryEntry *language = av_dict_get(stream->metadata, "language", NULL, 0);
    if (language) snprintf(s->language, sizeof s->language, "%.3s", language->value);
    s->duration_ms = stream->duration != AV_NOPTS_VALUE ? to_ms(stream->duration, stream->time_base)
                                                        : to_ms(media->format->duration, AV_TIME_BASE_Q);
    if (s->kind == RT_MEDIA_VIDEO) {
        s->width = (uint32_t)p->width;
        s->height = (uint32_t)p->height;
        s->sample_aspect = p->sample_aspect_ratio.den ? (float)av_q2d(p->sample_aspect_ratio) : 0.0f;
    } else {
        s->channels = (uint32_t)p->ch_layout.nb_channels;
        s->rate = (uint32_t)p->sample_rate;
    }
    media->format_index[media->count++] = index;
}

rt_media *rt_media_open(const char *host_path, char *error, size_t error_size)
{
    rt_media *media = calloc(1, sizeof *media);
    if (!media) return say(error, error_size, "out of memory"), NULL;
    media->video.stream = media->audio.stream = -1;
    int status = avformat_open_input(&media->format, host_path, NULL, NULL);
    if (status >= 0) status = avformat_find_stream_info(media->format, NULL);
    if (status < 0) {
        char reason[128];
        av_strerror(status, reason, sizeof reason);
        say(error, error_size, "%s: %s", host_path, reason);
        rt_media_close(media);
        return NULL;
    }
    for (unsigned i = 0; i < media->format->nb_streams; i++) list_stream(media, (int)i);
    media->frame = av_frame_alloc();
    if (!media->count || !media->frame) {
        say(error, error_size, "%s: no video or audio stream this build can decode", host_path);
        rt_media_close(media);
        return NULL;
    }
    return media;
}

static void drop_queue(decoder *d)
{
    while (d->head) {
        queued_packet *q = d->head;
        d->head = q->next;
        av_packet_free(&q->packet);
        free(q);
    }
    d->tail = NULL;
}

void rt_media_close(rt_media *media)
{
    if (!media) return;
    drop_queue(&media->video);
    drop_queue(&media->audio);
    avcodec_free_context(&media->video.codec);
    avcodec_free_context(&media->audio.codec);
    av_frame_free(&media->frame);
    sws_freeContext(media->scaler);
    swr_free(&media->resampler);
    free(media->pending);
    avformat_close_input(&media->format);
    free(media);
}

uint32_t rt_media_stream_count(const rt_media *media) { return media->count; }

const rt_media_stream *rt_media_stream_info(const rt_media *media, uint32_t index)
{
    return index < media->count ? &media->streams[index] : NULL;
}

int rt_media_select(rt_media *media, uint32_t index)
{
    if (index >= media->count) return -1;
    decoder *d = media->streams[index].kind == RT_MEDIA_VIDEO ? &media->video : &media->audio;
    const AVStream *stream = media->format->streams[media->format_index[index]];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context || avcodec_parameters_to_context(context, stream->codecpar) < 0 ||
        avcodec_open2(context, codec, NULL) < 0) {
        avcodec_free_context(&context);
        return -1;
    }
    drop_queue(d);
    avcodec_free_context(&d->codec);
    d->codec = context;
    d->stream = media->format_index[index];
    d->draining = 0;
    return 0;
}

static int enqueue(decoder *d, AVPacket *packet)
{
    queued_packet *q = malloc(sizeof *q);
    if (!q) return -1;
    q->packet = packet;
    q->next = NULL;
    if (d->tail) d->tail->next = q; else d->head = q;
    d->tail = q;
    return 0;
}

/* The next packet for decoder d: one already queued, or read on until one comes (queueing the
   other decoder's on the way). NULL at the end of the file or on a read error. */
static AVPacket *next_packet(rt_media *media, decoder *d)
{
    if (d->head) {
        queued_packet *q = d->head;
        d->head = q->next;
        if (!d->head) d->tail = NULL;
        AVPacket *packet = q->packet;
        free(q);
        return packet;
    }
    while (!media->demuxer_ended) {
        AVPacket *packet = av_packet_alloc();
        if (!packet) return NULL;
        if (av_read_frame(media->format, packet) < 0) {
            av_packet_free(&packet);
            media->demuxer_ended = 1;
            break;
        }
        if (packet->stream_index == d->stream) return packet;
        decoder *other = d == &media->video ? &media->audio : &media->video;
        if (packet->stream_index != other->stream || enqueue(other, packet) < 0) av_packet_free(&packet);
    }
    return NULL;
}

/* The next decoded frame of decoder d into media->frame: 1, 0 at the end, -1 on an error. */
static int decode(rt_media *media, decoder *d, char *error, size_t error_size)
{
    if (d->stream < 0 || !d->codec) return say(error, error_size, "no stream of this kind selected"), -1;
    for (;;) {
        int status = avcodec_receive_frame(d->codec, media->frame);
        if (status == 0) return 1;
        if (status == AVERROR_EOF) return 0;
        if (status != AVERROR(EAGAIN)) return say(error, error_size, "decoder error %d", status), -1;
        if (d->draining) return 0;
        AVPacket *packet = next_packet(media, d);
        if (!packet) d->draining = 1; /* a NULL packet tells the decoder to give out what it holds */
        status = avcodec_send_packet(d->codec, packet);
        av_packet_free(&packet);
        if (status < 0 && status != AVERROR_EOF) return say(error, error_size, "decoder error %d", status), -1;
    }
}

static uint64_t frame_time_ms(const rt_media *media, const decoder *d)
{
    const AVStream *stream = media->format->streams[d->stream];
    int64_t time = media->frame->best_effort_timestamp;
    if (time != AV_NOPTS_VALUE && stream->start_time != AV_NOPTS_VALUE) time -= stream->start_time;
    return to_ms(time, stream->time_base);
}

int rt_media_next_video(rt_media *media, uint8_t *nv12, uint32_t pitch, uint32_t rows, uint64_t *time_ms,
                        char *error, size_t error_size)
{
    const int got = decode(media, &media->video, error, error_size);
    if (got <= 0) return got;
    AVFrame *f = media->frame;
    if ((uint32_t)f->width > pitch || (uint32_t)f->height > rows)
        return av_frame_unref(f), say(error, error_size, "a %dx%d picture larger than the buffer", f->width, f->height), -1;
    media->scaler = sws_getCachedContext(media->scaler, f->width, f->height, (enum AVPixelFormat)f->format, f->width,
                                         f->height, AV_PIX_FMT_NV12, SWS_POINT, NULL, NULL, NULL);
    if (!media->scaler) return av_frame_unref(f), say(error, error_size, "no conversion to NV12"), -1;
    uint8_t *planes[4] = {nv12, nv12 + (size_t)pitch * rows, NULL, NULL};
    const int strides[4] = {(int)pitch, (int)pitch, 0, 0};
    sws_scale(media->scaler, (const uint8_t *const *)f->data, f->linesize, 0, f->height, planes, strides);
    *time_ms = frame_time_ms(media, &media->video);
    av_frame_unref(f);
    return 1;
}

/* Converts the decoded audio frame into media->pending. 0, or -1 on an error. */
static int convert_audio(rt_media *media, char *error, size_t error_size)
{
    AVFrame *f = media->frame;
    if (!media->resampler) {
        if (swr_alloc_set_opts2(&media->resampler, &f->ch_layout, AV_SAMPLE_FMT_S16, f->sample_rate, &f->ch_layout,
                                (enum AVSampleFormat)f->format, f->sample_rate, 0, NULL) < 0 ||
            swr_init(media->resampler) < 0)
            return say(error, error_size, "no conversion to 16-bit samples"), -1;
    }
    const uint32_t channels = (uint32_t)f->ch_layout.nb_channels;
    const uint32_t needed = (uint32_t)f->nb_samples * channels;
    if (needed > media->pending_capacity) {
        int16_t *grown = realloc(media->pending, needed * sizeof *grown);
        if (!grown) return say(error, error_size, "out of memory"), -1;
        media->pending = grown;
        media->pending_capacity = needed;
    }
    uint8_t *out[1] = {(uint8_t *)media->pending};
    const int converted = swr_convert(media->resampler, out, f->nb_samples, (const uint8_t **)f->extended_data, f->nb_samples);
    if (converted < 0) return say(error, error_size, "sample conversion error %d", converted), -1;
    media->pending_frames = (uint32_t)converted;
    media->pending_offset = 0;
    media->pending_time_ms = frame_time_ms(media, &media->audio);
    return 0;
}

int rt_media_next_audio(rt_media *media, int16_t *pcm, uint32_t max_frames, uint32_t *frames, uint64_t *time_ms,
                        char *error, size_t error_size)
{
    if (media->pending_offset >= media->pending_frames) {
        const int got = decode(media, &media->audio, error, error_size);
        if (got <= 0) return got;
        const int converted = convert_audio(media, error, error_size);
        av_frame_unref(media->frame);
        if (converted < 0) return -1;
    }
    const uint32_t channels = (uint32_t)media->audio.codec->ch_layout.nb_channels;
    const uint32_t rate = (uint32_t)media->audio.codec->sample_rate;
    uint32_t n = media->pending_frames - media->pending_offset;
    if (n > max_frames) n = max_frames;
    memcpy(pcm, media->pending + (size_t)media->pending_offset * channels, (size_t)n * channels * sizeof *pcm);
    *time_ms = media->pending_time_ms + (rate ? (uint64_t)media->pending_offset * 1000 / rate : 0);
    media->pending_offset += n;
    *frames = n;
    return 1;
}

int rt_media_rewind(rt_media *media)
{
    if (av_seek_frame(media->format, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) return -1;
    decoder *both[2] = {&media->video, &media->audio};
    for (int i = 0; i < 2; i++) {
        drop_queue(both[i]);
        if (both[i]->codec) avcodec_flush_buffers(both[i]->codec);
        both[i]->draining = 0;
    }
    media->pending_frames = media->pending_offset = 0;
    media->demuxer_ended = 0;
    return 0;
}
