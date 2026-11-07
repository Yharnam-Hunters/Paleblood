/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceAudioOut's movie port (runtime/syslib/audio_out.c): 1024 samples at 48000 Hz in the 16-bit
   and float formats 0 to 5, opened beside the main ports the main tests open, and paced one block
   per 1024 / 48000 s like them. Other lengths, rates and formats still stop by name. */
#include "../syslib_test.h"

#include <stdint.h>
#include <time.h>

typedef int32_t (*f_ao_init)(void);
typedef int32_t (*f_ao_open)(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*f_ao_volume)(int32_t, int32_t, const int32_t *);
typedef int32_t (*f_ao_output)(int32_t, const void *);

static f_ao_open movie_open;
static void open_movie_other_rate(void) { movie_open(0xff, 0, 0, 1024, 44100, 2); }
static void open_movie_standard_order(void) { movie_open(0xff, 0, 0, 1024, 48000, 6); }
static void open_main_s16(void) { movie_open(0xff, 0, 0, 256, 48000, 2); }

SYSLIB_TEST(audio_out_movie)
{
    const f_ao_init init = (f_ao_init)fn("libSceAudioOut", "sceAudioOutInit");
    movie_open = (f_ao_open)fn("libSceAudioOut", "sceAudioOutOpen");
    const f_ao_volume volume = (f_ao_volume)fn("libSceAudioOut", "sceAudioOutSetVolume");
    const f_ao_output output = (f_ao_output)fn("libSceAudioOut", "sceAudioOutOutput");
    if (!init || !movie_open || !volume || !output) return;
    const int32_t first = init();
    CHECK(first == 0 || first == (int32_t)0x8026000Eu); /* the main tests may have initialized it */

    const int32_t eight = movie_open(0xff, 0, 0, 1024, 48000, 2); /* the 6-channel movie: 16-bit, 8 channels */
    CHECK(eight > 0);
    static const int32_t unity[8] = {32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768};
    CHECK(volume(eight, 0xff, unity) == 0);
    static int16_t block[1024 * 8];
    CHECK(output(eight, block) == 0); /* an idle port takes the first block at once */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 5; i++) CHECK(output(eight, block) == 0); /* 5 x 21.3 ms */
    clock_gettime(CLOCK_MONOTONIC, &end);
    const long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000L + (end.tv_nsec - start.tv_nsec) / 1000;
    CHECK(elapsed_us >= 100000 && elapsed_us < 300000);

    CHECK(movie_open(0xff, 0, 0, 1024, 48000, 1) > 0); /* a stereo movie */
    CHECK(died_in_fatal(open_movie_other_rate));
    CHECK(died_in_fatal(open_movie_standard_order));
    CHECK(died_in_fatal(open_main_s16));
}
