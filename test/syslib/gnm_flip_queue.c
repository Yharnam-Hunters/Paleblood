/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The flip after a frame's work (runtime/syslib/gnm.c, video_out.c): sceGnmSubmitAndFlipCommandBuffers
   queues its vsync flip, which waits for its vertical blank on a thread of its own, so the thread
   that finishes the GPU's work (here the caller: with the display off the completion steps are
   performed at once) is not held for it. Queued flips take effect in order, one per vertical blank,
   and sceVideoOutSubmitFlip comes after the flips queued before it. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

typedef int32_t (*vo_open_fn)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*vo_close_fn)(int32_t);
typedef uint64_t (*vo_attr_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*vo_register_fn)(int32_t, int32_t, void *const *, int32_t, const void *);
typedef int32_t (*vo_flip_fn)(int32_t, int32_t, int32_t, int64_t);
typedef int32_t (*vo_status_fn)(int32_t, void *);
typedef int32_t (*gnm_flip_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *, int32_t, int32_t, int32_t, int64_t);

enum { FLIP_VSYNC = 1, STATUS_BYTES = 64, STATUS_ARGUMENT = 0x18, STATUS_CURRENT_BUFFER = 0x38 };
enum { VBLANK_NS = 1000000000 / 60, SHOWN_WITHIN_NS = 1000000000 };

static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

typedef struct {
    uint64_t count;
    int64_t argument;
    int32_t current_buffer;
} flip_state;

static flip_state read_status(vo_status_fn status, int32_t handle)
{
    uint8_t raw[STATUS_BYTES];
    flip_state s = {0, 0, -1};
    CHECK(status(handle, raw) == 0);
    memcpy(&s.count, raw, sizeof s.count);
    memcpy(&s.argument, raw + STATUS_ARGUMENT, sizeof s.argument);
    memcpy(&s.current_buffer, raw + STATUS_CURRENT_BUFFER, sizeof s.current_buffer);
    return s;
}

/* Polls the flip status until `count` flips have been shown, or about a second has passed. */
static flip_state wait_for_flips(vo_status_fn status, int32_t handle, uint64_t count)
{
    const int64_t give_up = now_ns() + SHOWN_WITHIN_NS;
    const struct timespec poll = {0, 1000000};
    flip_state s = read_status(status, handle);
    while (s.count < count && now_ns() < give_up) {
        nanosleep(&poll, NULL);
        s = read_status(status, handle);
    }
    return s;
}

SYSLIB_TEST(gnm_flip_queue)
{
    const vo_open_fn vo_open = (vo_open_fn)fn("libSceVideoOut", "sceVideoOutOpen");
    const vo_close_fn vo_close = (vo_close_fn)fn("libSceVideoOut", "sceVideoOutClose");
    const vo_attr_fn vo_attr = (vo_attr_fn)fn("libSceVideoOut", "sceVideoOutSetBufferAttribute");
    const vo_register_fn vo_register = (vo_register_fn)fn("libSceVideoOut", "sceVideoOutRegisterBuffers");
    const vo_flip_fn vo_flip = (vo_flip_fn)fn("libSceVideoOut", "sceVideoOutSubmitFlip");
    const vo_status_fn vo_status = (vo_status_fn)fn("libSceVideoOut", "sceVideoOutGetFlipStatus");
    const gnm_flip_fn gnm_flip = (gnm_flip_fn)fn("libSceGnmDriver", "sceGnmSubmitAndFlipCommandBuffers");
    if (!vo_open || !vo_close || !vo_attr || !vo_register || !vo_flip || !vo_status || !gnm_flip) return;

    const int32_t handle = vo_open(0, 0, 0, NULL);
    CHECK(handle > 0);
    uint32_t attribute[10];
    vo_attr(attribute, 0x80000000u, 0, 0, 4, 4, 4);
    static uint32_t frame_a[16], frame_b[16];
    void *const frames[2] = {frame_a, frame_b};
    CHECK(vo_register(handle, 0, frames, 2, attribute) == 0);

    uint32_t work[2] = {0xc0001000u, 0}; /* one NOP: a frame with nothing to draw */
    void *buffers[1] = {work};
    uint32_t bytes[1] = {sizeof work};
    /* a first flip, shown, so that what starts once (the display's frames, the flip thread) has */
    const uint64_t first = read_status(vo_status, handle).count;
    CHECK(gnm_flip(1, buffers, bytes, NULL, NULL, handle, 1, FLIP_VSYNC, 100) == 0);
    CHECK(wait_for_flips(vo_status, handle, first + 1).count == first + 1);
    uint64_t before = first + 1;

    /* Two vsync flips in a row take effect on two different vertical blanks, at least one period
       apart; the caller is back from both before the second has been shown (holding the caller
       for each vertical blank, it came back only after both). A loaded host may hold the test
       thread itself past a vertical blank, so the pair is tried a few times: one early return
       shows the caller was not held. */
    enum { ATTEMPTS = 5 };
    int returned_early = 0;
    for (int attempt = 0; attempt < ATTEMPTS && !returned_early; attempt++) {
        const int64_t start = now_ns();
        CHECK(gnm_flip(1, buffers, bytes, NULL, NULL, handle, 0, FLIP_VSYNC, 101) == 0);
        CHECK(gnm_flip(1, buffers, bytes, NULL, NULL, handle, 1, FLIP_VSYNC, 102) == 0);
        returned_early = read_status(vo_status, handle).count < before + 2;
        const flip_state shown = wait_for_flips(vo_status, handle, before + 2);
        CHECK(shown.count == before + 2 && shown.argument == 102 && shown.current_buffer == 1);
        CHECK(now_ns() - start >= VBLANK_NS - 1000000); /* the second flip waited for its own vertical blank */
        before = shown.count;
    }
    CHECK(returned_early);

    /* sceVideoOutSubmitFlip after a queued flip: the queued one is shown first, then its own. */
    CHECK(gnm_flip(1, buffers, bytes, NULL, NULL, handle, 0, FLIP_VSYNC, 103) == 0);
    CHECK(vo_flip(handle, 1, FLIP_VSYNC, 104) == 0);
    const flip_state after = read_status(vo_status, handle);
    CHECK(after.count == before + 2 && after.argument == 104 && after.current_buffer == 1);
    CHECK(vo_close(handle) == 0);
}
