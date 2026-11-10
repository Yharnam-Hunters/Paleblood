/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceAudioOutClose (runtime/syslib/audio_out.c), as the movie player's sound ends: no block
   (sceAudioOutOutput with NULL), then the close, which answers 0 and frees the handle for the next
   open (the lowest free handle); a closed handle is refused by output and close alike. */
#include "../syslib_test.h"

#include <stdint.h>

typedef int32_t (*f_ao_init)(void);
typedef int32_t (*f_ao_open)(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*f_ao_output)(int32_t, const void *);
typedef int32_t (*f_ao_close)(int32_t);

static f_ao_output output_fn;
static f_ao_close close_fn;
static int32_t closed_handle;
static void output_after_close(void) { output_fn(closed_handle, NULL); }
static void close_twice(void) { close_fn(closed_handle); }

SYSLIB_TEST(audio_out_close)
{
    const f_ao_init init = (f_ao_init)fn("libSceAudioOut", "sceAudioOutInit");
    const f_ao_open open_port = (f_ao_open)fn("libSceAudioOut", "sceAudioOutOpen");
    output_fn = (f_ao_output)fn("libSceAudioOut", "sceAudioOutOutput");
    close_fn = (f_ao_close)fn("libSceAudioOut", "sceAudioOutClose");
    CHECK(is_implemented("libSceAudioOut", "sceAudioOutClose"));
    if (!init || !open_port || !output_fn || !close_fn) return;
    const int32_t first = init();
    CHECK(first == 0 || first == (int32_t)0x8026000Eu); /* the main tests may have initialized it */
    const int32_t movie = open_port(0xff, 0, 0, 1024, 48000, 1); /* the movie player's stereo port */
    CHECK(movie > 0);
    static int16_t block[1024 * 2];
    CHECK(output_fn(movie, block) == 0);
    CHECK(output_fn(movie, NULL) == 0 && close_fn(movie) == 0); /* the movie's end */
    const int32_t again = open_port(0xff, 0, 0, 1024, 48000, 1);
    CHECK(again == movie); /* the lowest free handle: the one just closed */
    CHECK(close_fn(again) == 0);
    closed_handle = again;
    CHECK(died_in_fatal(output_after_close));
    CHECK(died_in_fatal(close_twice));
}
