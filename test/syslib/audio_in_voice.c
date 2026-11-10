/* SPDX-License-Identifier: GPL-2.0-or-later */
/* No microphone and no voice chat (runtime/syslib/audio_in.c, voice.c): an input port opens with
   the executable's arguments, reads silence and answers its sample count, and closes; the argument
   checks answer shadPS4's codes. The voice library initializes, its ports carry no sound, and
   misuse stops by name. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define INVALID_HANDLE ((int32_t)0x80260101u)
#define INVALID_SIZE ((int32_t)0x80260102u)
#define INVALID_FREQ ((int32_t)0x80260103u)
#define INVALID_POINTER ((int32_t)0x80260105u)
#define INVALID_PARAM ((int32_t)0x80260106u)

typedef int32_t (*open_fn)(int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*input_fn)(int32_t, void *);
typedef int32_t (*close_fn)(int32_t);
typedef int32_t (*init_fn)(const void *, uint32_t);
typedef int32_t (*void_fn)(void);
typedef int32_t (*create_fn)(uint32_t *, const int32_t *);
typedef int32_t (*id_fn)(uint32_t);
typedef int32_t (*pair_fn)(uint32_t, uint32_t);
typedef int32_t (*start_fn)(const void *);
typedef int32_t (*info_fn)(uint32_t, void *);
typedef int32_t (*read_fn)(uint32_t, void *, uint32_t *);
typedef int32_t (*write_fn)(uint32_t, const void *, uint32_t *, int16_t);

static open_fn in_open;
static init_fn voice_init;
static void_fn voice_stop, voice_end;
static create_fn create_port;
static info_fn get_info;
static pair_fn connect_ports;
static read_fn read_port;

static void open_type_2(void) { in_open(1, 2, 0, 256, 16000, 0); }
static void open_index_1(void) { in_open(1, 0, 1, 256, 16000, 0); }
static void open_until_full(void)
{
    for (int i = 0; i < 100; i++) in_open(1, 1, 0, 256, 16000, 0);
}
static uint8_t voice_param[0x28];
static void init_twice(void) { voice_init(voice_param, 100); }
static void init_null(void) { voice_init(NULL, 100); }
static void stop_before_init(void) { voice_stop(); }
static void info_unknown_port(void)
{
    uint8_t info[0x20];
    get_info(31, info);
}
static void info_null(void) { get_info(0, NULL); }
static void connect_unknown(void) { connect_ports(0, 30); }
static void create_null(void) { create_port(NULL, NULL); }
static void create_until_full(void)
{
    const int32_t param[6] = {0};
    uint32_t id;
    for (int i = 0; i < 100; i++) create_port(&id, param);
}
static void read_null(void) { read_port(0, NULL, NULL); }
static start_fn voice_start;
static write_fn write_port_entry;
static void start_null(void) { voice_start(NULL); }
static void write_null(void) { write_port_entry(1, NULL, NULL, 0); }

SYSLIB_TEST(audio_in_voice)
{
    in_open = (open_fn)fn("libSceAudioIn", "sceAudioInOpen");
    const input_fn input = (input_fn)fn("libSceAudioIn", "sceAudioInInput");
    const close_fn in_close = (close_fn)fn("libSceAudioIn", "sceAudioInClose");
    CHECK(is_implemented("libSceAudioIn", "sceAudioInOpen"));
    if (!in_open || !input || !in_close) return;

    /* The executable's open: 256 samples, 16 kHz, 16-bit mono; handles from 1. */
    const int32_t h = in_open(1, 0, 0, 256, 16000, 0);
    CHECK(h == 1);
    int16_t samples[2 * 2048 + 4];
    memset(samples, 0x55, sizeof samples);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(input(h, samples) == 256);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const int64_t us = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
    CHECK(us >= 15000); /* 256 / 16000 s = 16 ms */
    int silent = 1;
    for (int i = 0; i < 256; i++) silent &= samples[i] == 0;
    CHECK(silent && samples[256] == 0x5555);
    CHECK(input(h, NULL) == INVALID_POINTER);
    /* Stereo at 48 kHz: twice the samples written. */
    const int32_t stereo = in_open(1, 5, 0, 8, 48000, 1);
    CHECK(stereo == 2);
    memset(samples, 0x55, sizeof samples);
    CHECK(input(stereo, samples) == 8);
    silent = 1;
    for (int i = 0; i < 16; i++) silent &= samples[i] == 0;
    CHECK(silent && samples[16] == 0x5555);
    /* The checks. */
    CHECK(in_open(1, 1, 0, 0, 16000, 0) == INVALID_SIZE && in_open(1, 1, 0, 2049, 16000, 0) == INVALID_SIZE);
    CHECK(in_open(1, 1, 0, 2048, 16000, 2) == INVALID_PARAM);
    CHECK(in_open(1, 1, 0, 256, 44100, 0) == INVALID_FREQ);
    CHECK(died_in_fatal(open_type_2) && died_in_fatal(open_index_1) && died_in_fatal(open_until_full));
    CHECK(input(0, samples) == INVALID_HANDLE && input(9, samples) == INVALID_HANDLE && input(3, samples) == INVALID_HANDLE);
    CHECK(in_close(h) == 0 && in_close(h) == INVALID_HANDLE && input(h, samples) == INVALID_HANDLE);
    CHECK(in_open(1, 1, 0, 256, 16000, 0) == 1); /* the freed handle comes back */
    CHECK(in_close(1) == 0 && in_close(stereo) == 0 && in_close(0) == INVALID_HANDLE && in_close(9) == INVALID_HANDLE);

    voice_init = (init_fn)fn("libSceVoice", "sceVoiceInit");
    voice_end = (void_fn)fn("libSceVoice", "sceVoiceEnd");
    voice_stop = (void_fn)fn("libSceVoice", "sceVoiceStop");
    create_port = (create_fn)fn("libSceVoice", "sceVoiceCreatePort");
    const id_fn delete_port = (id_fn)fn("libSceVoice", "sceVoiceDeletePort");
    connect_ports = (pair_fn)fn("libSceVoice", "sceVoiceConnectIPortToOPort");
    const pair_fn disconnect = (pair_fn)fn("libSceVoice", "sceVoiceDisconnectIPortFromOPort");
    const start_fn start = (start_fn)fn("libSceVoice", "sceVoiceStart");
    get_info = (info_fn)fn("libSceVoice", "sceVoiceGetPortInfo");
    read_port = (read_fn)fn("libSceVoice", "sceVoiceReadFromOPort");
    const write_fn write_port = (write_fn)fn("libSceVoice", "sceVoiceWriteToIPort");
    CHECK(is_implemented("libSceVoice", "sceVoiceInit"));
    if (!voice_init || !voice_end || !voice_stop || !create_port || !delete_port || !connect_ports || !disconnect || !start ||
        !get_info || !read_port || !write_port)
        return;
    CHECK(died_in_fatal(stop_before_init) && died_in_fatal(init_null));
    CHECK(voice_init(voice_param, 100) == 0);
    CHECK(died_in_fatal(init_twice));
    const int32_t mic[6] = {0}, speaker[6] = {5};
    uint32_t mic_id = 99, speaker_id = 99;
    CHECK(create_port(&mic_id, mic) == 0 && mic_id == 0 && create_port(&speaker_id, speaker) == 0 && speaker_id == 1);
    CHECK(died_in_fatal(create_null) && died_in_fatal(create_until_full));
    uint8_t info[0x20];
    memset(info, 0xee, sizeof info);
    CHECK(get_info(speaker_id, info) == 0);
    int32_t type;
    uint32_t bytes, frame;
    memcpy(&type, info, 4), memcpy(&bytes, info + 0x10, 4), memcpy(&frame, info + 0x14, 4);
    CHECK(type == 5 && bytes == 0 && frame == 1 && info[4] == 0 && info[0x18] == 0 && info[0x1f] == 0);
    CHECK(died_in_fatal(info_unknown_port) && died_in_fatal(info_null) && died_in_fatal(connect_unknown));
    uint8_t buffer[16];
    uint32_t size = sizeof buffer;
    CHECK(read_port(mic_id, buffer, &size) == 0 && size == 0);
    CHECK(died_in_fatal(read_null));
    size = sizeof buffer;
    CHECK(write_port(speaker_id, buffer, &size, 0) == 0);
    CHECK(connect_ports(mic_id, speaker_id) == 0 && disconnect(mic_id, speaker_id) == 0);
    CHECK(start(buffer) == 0 && voice_stop() == 0);
    voice_start = start, write_port_entry = write_port;
    CHECK(died_in_fatal(start_null) && died_in_fatal(write_null));
    CHECK(delete_port(mic_id) == 0 && delete_port(0xff) == 0);
    uint32_t again = 99;
    CHECK(create_port(&again, mic) == 0 && again == 0); /* the freed id comes back */
    CHECK(voice_end() == 0);
    CHECK(died_in_fatal(stop_before_init));
    CHECK(voice_init(voice_param, 100) == 0 && create_port(&again, mic) == 0 && again == 0); /* End forgot the ports */
    CHECK(voice_end() == 0);
}
