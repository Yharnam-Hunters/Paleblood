/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Graphics events and the last GNM calls (runtime/syslib/gnm.c): sceGnmAddEqEvent and
   sceGnmDeleteEqEvent, raised by the end-of-work packets' interrupts in the GPU's completion step;
   sceGnmRequestFlipAndSubmitDone; sceGnmGetEqTimeStamp, sceGnmDebugHardwareStatus and the logical
   compute unit mappings. With the display off the completion steps are performed at once. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef struct rt_equeue rt_equeue;
typedef int32_t (*create_fn)(rt_equeue **, const char *);
typedef int32_t (*wait_fn)(rt_equeue *, uint8_t *, int32_t, int32_t *, const void *);
typedef int32_t (*delete_queue_fn)(rt_equeue *);
typedef int32_t (*add_fn)(rt_equeue *, uint64_t, void *);
typedef int32_t (*del_fn)(rt_equeue *, uint64_t);
typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);
typedef int32_t (*map_fn)(uint32_t, uint32_t, void *, uint32_t, volatile uint32_t *);
typedef void (*ding_fn)(uint32_t, uint32_t);
typedef int32_t (*unmap_fn)(uint32_t);
typedef int32_t (*type_fn)(const uint8_t *);
typedef int32_t (*flip_fn)(uint32_t *, uint32_t, int32_t, int32_t, int32_t, int64_t);
typedef int32_t (*void_fn)(void);
typedef uint32_t (*cu_fn)(uint32_t, uint32_t);
typedef int32_t (*vo_open_fn)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*vo_close_fn)(int32_t);
typedef int32_t (*vo_event_fn)(rt_equeue *, int32_t, void *);
typedef uint64_t (*vo_attr_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*vo_register_fn)(int32_t, int32_t, void *const *, int32_t, const void *);

enum { EOP = 0x40, EBADF_SCE = (int32_t)0x80020009u, EVENT_BYTES = 32 };

static volatile uint32_t label;
static submit_fn submit;
static add_fn add;
static del_fn del;
static rt_equeue *events;

/* A graphics command buffer with one EVENT_WRITE_EOP: 32-bit data, the given interrupt select. */
static void submit_eop(uint32_t interrupt_select, uint32_t value)
{
    const uint64_t at = (uint64_t)(uintptr_t)&label;
    uint32_t packets[] = {0xc0044700u, 0x528, (uint32_t)at,
                          (uint32_t)(at >> 32) | 0x20000000u | interrupt_select << 24, value, 0};
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {sizeof packets};
    submit(1, buffers, bytes, NULL, NULL);
}
static void eop_select_three(void) { submit_eop(3, 1); }
static void delete_unregistered(void) { del(events, 0x41); }
static void delete_eop_again(void) { del(events, EOP); }

/* A compute queue on pipe `pipe` whose one indirect buffer holds a RELEASE_MEM with an interrupt. */
static uint32_t ring[0x40];
static volatile uint32_t read_pointer;
static uint32_t job[7];
static map_fn map;
static ding_fn ding;
static unmap_fn unmap;
static void release_on_pipe(uint32_t pipe, uint32_t interrupt_select)
{
    const int32_t queue = map(pipe, 3, ring, 0x40, &read_pointer);
    const uint64_t at = (uint64_t)(uintptr_t)&label;
    const uint32_t packets[7] = {0xc0054900u, 0x0003862fu, 0x20000000u | interrupt_select << 24, (uint32_t)at, (uint32_t)(at >> 32), 5, 0};
    memcpy(job, packets, sizeof job);
    const uint64_t jb = (uint64_t)(uintptr_t)job;
    ring[0] = 0xc0023f00u, ring[1] = (uint32_t)jb, ring[2] = (uint32_t)(jb >> 32) & 0xffff, ring[3] = 7;
    ding((uint32_t)queue, 4);
    unmap((uint32_t)queue);
}
static void release_on_pipe_seven(void) { release_on_pipe(7, 2); }

static uint64_t field(const uint8_t *event, int offset)
{
    uint64_t v = 0;
    memcpy(&v, event + offset, offset == 8 ? 2 : 8);
    return v;
}

SYSLIB_TEST(gnm_events)
{
    const create_fn create = (create_fn)fn("libkernel", "sceKernelCreateEqueue");
    const wait_fn wait = (wait_fn)fn("libkernel", "sceKernelWaitEqueue");
    const delete_queue_fn delete_queue = (delete_queue_fn)fn("libkernel", "sceKernelDeleteEqueue");
    add = (add_fn)fn("libSceGnmDriver", "sceGnmAddEqEvent");
    del = (del_fn)fn("libSceGnmDriver", "sceGnmDeleteEqEvent");
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    map = (map_fn)fn("libSceGnmDriver", "sceGnmMapComputeQueue");
    ding = (ding_fn)fn("libSceGnmDriver", "sceGnmDingDong");
    unmap = (unmap_fn)fn("libSceGnmDriver", "sceGnmUnmapComputeQueue");
    const type_fn event_type = (type_fn)fn("libSceGnmDriver", "sceGnmGetEqEventType");
    CHECK(is_implemented("libSceGnmDriver", "sceGnmAddEqEvent") && is_implemented("libSceGnmDriver", "sceGnmDeleteEqEvent"));
    if (!create || !wait || !delete_queue || !add || !del || !submit || !map || !ding || !unmap || !event_type) return;

    static int user_eop, user_pipe;
    CHECK(add(NULL, EOP, &user_eop) == EBADF_SCE && del(NULL, EOP) == EBADF_SCE);
    CHECK(create(&events, "gnm") == 0 && events);
    CHECK(add(events, EOP, &user_pipe) == 0 && add(events, EOP, &user_eop) == 0); /* again: new user data */
    CHECK(add(events, 2, &user_pipe) == 0);

    /* No interrupt asked for: the write lands, no event. Then the compute pipe's interrupt alone:
       a wait with room for two gets only that one, so the first raised nothing. */
    uint8_t got[2 * EVENT_BYTES];
    int32_t n = 0;
    label = 0;
    submit_eop(0, 9);
    CHECK(label == 9);
    release_on_pipe(2, 2);
    CHECK(label == 5);
    CHECK(wait(events, got, 2, &n, NULL) == 0 && n == 1);
    CHECK(field(got, 0) == 2 && (int16_t)field(got, 8) == -14 && field(got, 16) == 2);
    CHECK(field(got, 24) == (uint64_t)(uintptr_t)&user_pipe);

    /* Interrupt after the write (2) and interrupt only (1): the end-of-pipe event, data 0x40. */
    for (uint32_t select = 1; select <= 2; select++) {
        label = 0;
        submit_eop(select, 3);
        CHECK(label == 3);
        CHECK(wait(events, got, 2, &n, NULL) == 0 && n == 1);
        CHECK(field(got, 0) == EOP && (int16_t)field(got, 8) == -14 && field(got, 16) == EOP);
        CHECK(field(got, 24) == (uint64_t)(uintptr_t)&user_eop && event_type(got) == EOP);
    }
    /* A RELEASE_MEM in a graphics buffer raises the end of pipe too. */
    {
        const uint64_t at = (uint64_t)(uintptr_t)&label;
        uint32_t packets[] = {0xc0054900u, 0x0003862fu, 0x22000000u, (uint32_t)at, (uint32_t)(at >> 32), 4, 0};
        void *buffers[1] = {packets};
        uint32_t bytes[1] = {sizeof packets};
        CHECK(submit(1, buffers, bytes, NULL, NULL) == 0 && label == 4);
        CHECK(wait(events, got, 2, &n, NULL) == 0 && n == 1 && field(got, 0) == EOP);
    }

    /* Deleted: a trigger not yet delivered goes with it, the end of pipe raises nothing on this
       queue any more, and a second delete has nothing to delete (registered twice above, once). */
    submit_eop(2, 1);
    CHECK(del(events, EOP) == 0);
    CHECK(died_in_fatal(delete_eop_again));
    submit_eop(2, 1);
    release_on_pipe(2, 3);
    CHECK(wait(events, got, 2, &n, NULL) == 0 && n == 1 && field(got, 0) == 2);
    CHECK(died_in_fatal(delete_unregistered));
    CHECK(died_in_fatal(eop_select_three));
    CHECK(died_in_fatal(release_on_pipe_seven));
    CHECK(del(events, 2) == 0);
    CHECK(delete_queue(events) == 0);

    /* The queries nothing in the executable reaches, and the identity compute unit mapping. */
    const void_fn timestamp = (void_fn)fn("libSceGnmDriver", "sceGnmGetEqTimeStamp");
    const void_fn hardware = (void_fn)fn("libSceGnmDriver", "sceGnmDebugHardwareStatus");
    const cu_fn cu_index = (cu_fn)fn("libSceGnmDriver", "sceGnmLogicalCuIndexToPhysicalCuIndex");
    const cu_fn cu_mask = (cu_fn)fn("libSceGnmDriver", "sceGnmLogicalCuMaskToPhysicalCuMask");
    if (timestamp && hardware && cu_index && cu_mask) {
        CHECK(timestamp() == 0 && hardware() == 0);
        CHECK(cu_index(1, 5) == 5 && cu_index(0, 9) == 9);
        CHECK(cu_mask(1, 0x3ff) == 0x3ff && cu_mask(0, 0x0f0) == 0x0f0);
    }
}

static flip_fn request_flip;
static void flip_without_buffer(void) { request_flip(NULL, 0x100, 1, 0, 2, 0); }

SYSLIB_TEST(gnm_request_flip)
{
    request_flip = (flip_fn)fn("libSceGnmDriver", "sceGnmRequestFlipAndSubmitDone");
    const create_fn create = (create_fn)fn("libkernel", "sceKernelCreateEqueue");
    const wait_fn wait = (wait_fn)fn("libkernel", "sceKernelWaitEqueue");
    const vo_open_fn vo_open = (vo_open_fn)fn("libSceVideoOut", "sceVideoOutOpen");
    const vo_close_fn vo_close = (vo_close_fn)fn("libSceVideoOut", "sceVideoOutClose");
    const vo_event_fn vo_event = (vo_event_fn)fn("libSceVideoOut", "sceVideoOutAddFlipEvent");
    const vo_attr_fn vo_attr = (vo_attr_fn)fn("libSceVideoOut", "sceVideoOutSetBufferAttribute");
    const vo_register_fn vo_register = (vo_register_fn)fn("libSceVideoOut", "sceVideoOutRegisterBuffers");
    CHECK(is_implemented("libSceGnmDriver", "sceGnmRequestFlipAndSubmitDone"));
    if (!request_flip || !create || !wait || !vo_open || !vo_close || !vo_event || !vo_attr || !vo_register) return;

    const int32_t handle = vo_open(0, 0, 0, NULL);
    rt_equeue *flips = NULL;
    CHECK(handle > 0 && create(&flips, "flips") == 0 && vo_event(flips, handle, NULL) == 0);
    uint32_t attribute[10];
    vo_attr(attribute, 0x80000000u, 0, 0, 4, 4, 4);
    static uint32_t frame_a[16], frame_b[16];
    void *const frames[2] = {frame_a, frame_b};
    CHECK(vo_register(handle, 0, frames, 2, attribute) == 0);

    static uint32_t command_buffer[0x40 + 4];
    memset(command_buffer, 0xee, sizeof command_buffer);
    CHECK(request_flip(command_buffer, 0xff, handle, 1, 2, 31) == (int32_t)0x80d11001u);
    CHECK(command_buffer[0] == 0xeeeeeeeeu); /* refused before writing */
    CHECK(request_flip(command_buffer, 0x100, handle, 1, 2, 31) == 0);
    CHECK(command_buffer[0] == 0xc03e1000u && command_buffer[0x40] == 0xeeeeeeeeu); /* one NOP over 0x100 bytes */
    uint8_t event[32];
    int32_t n = 0;
    int64_t data = 0;
    CHECK(wait(flips, event, 1, &n, NULL) == 0 && n == 1 && (memcpy(&data, event + 16, 8), data == 31));
    CHECK(died_in_fatal(flip_without_buffer));
    CHECK(vo_close(handle) == 0);
}
