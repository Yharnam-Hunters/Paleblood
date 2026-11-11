/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: video out (libSceVideoOut): the display, its frame buffers and flips.

   Facts used: the OpenOrbis homebrew SDK's VideoOut.h declares the calls, and its _types/video.h
   the 40-byte buffer attribute (format, tiling mode, aspect, width, height, pitch in pixels), the
   64-byte flip status (flip count, two times, the flip argument, pending counts, current buffer),
   flip rates (0: 60 Hz, 1: 30 Hz, 2: 20 Hz) and flip modes (1: vsync, 2: hsync). The executable
   sets up video out twice, in the same order (0x015e488e and 0x02ad61d4): open (0, 0, 0, no
   parameter), flip rate 0, an event queue with a flip event, a buffer attribute (format
   0x80000000, 1920 x 1080 in the first), then its buffers. Each frame it submits a flip with mode
   2 and waits on the queue (0x015e419a then 0x015e41b6); its other loop (0x02ad6ee9) reads the
   flip status until the flip argument reaches the frame it submitted.

   The runtime has no display yet: it keeps each port's buffers and status, and an hsync flip
   completes at once, a vsync flip at the next 60 Hz tick of the host's monotonic clock: the
   current buffer and flip argument change, the flip count grows and the port's flip events are
   triggered. A flip status before the first flip is all zero (no flips, nothing pending, buffer
   0). The PS4's initial flip argument is not established, but the only caller (its flip loop,
   calling at 0x02ad6ee9) reads it only once it has submitted as many frames as it has buffers,
   which takes a flip first; before that it only checks that the call succeeds. Other flip modes
   or rates, other open arguments, and errors stop by name. The flip event's ident is 0 and its data
   the flip argument: the PS4's values are not established and no caller reads them. */
#include "runtime/equeue.h"
#include "runtime/host_time.h"
#include "runtime/process.h"
#include "runtime/renderer.h"
#include "runtime/display.h"
#include "runtime/syslib.h"
#include "runtime/video_out.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum { FLIP_MODE_VSYNC = 1, FLIP_MODE_HSYNC = 2, MAX_PORTS = 4, MAX_BUFFERS = 16, MAX_FLIP_QUEUES = 4 };
enum { VBLANKS_PER_SECOND = 60 };
enum { FLIP_RATE_60HZ = 0, FLIP_RATE_30HZ = 1, FLIP_RATE_20HZ = 2 };

typedef struct {
    uint64_t count, process_time, system_time;
    int64_t flip_argument;
    uint64_t reserved[2];
    int32_t gpu_flips_pending, flips_pending, current_buffer;
    uint32_t reserved1;
} flip_status;
_Static_assert(sizeof(flip_status) == 64, "OrbisVideoOutFlipStatus is 64 bytes");

typedef struct {
    int open;
    int32_t flip_rate;
    void *buffers[MAX_BUFFERS];
    int buffer_count;
    rt_video_out_buffer_attribute attribute;
    rt_equeue *queues[MAX_FLIP_QUEUES];
    int queue_count;
    flip_status status;
} video_port;

static pthread_mutex_t ports_lock = PTHREAD_MUTEX_INITIALIZER;
static video_port ports[MAX_PORTS + 1]; /* handles from 1 */

static video_port *port_of(const char *caller, int32_t handle)
{
    if (handle < 1 || handle > MAX_PORTS || !ports[handle].open)
        rt_fatal(caller, "a handle the runtime did not hand out: the error value is not established");
    return &ports[handle];
}

static int32_t video_out_open(int32_t user, int32_t bus_type, int32_t index, const void *parameter)
{
    static const char fn[] = "sceVideoOutOpen";
    if (user || bus_type || index || parameter) rt_fatal(fn, "arguments other than the observed (0, 0, 0, none)");
    pthread_mutex_lock(&ports_lock);
    int32_t handle = 1;
    while (handle <= MAX_PORTS && ports[handle].open) handle++;
    if (handle > MAX_PORTS) rt_fatal(fn, "more open ports than the runtime holds");
    ports[handle] = (video_port){.open = 1};
    pthread_mutex_unlock(&ports_lock);
    return handle;
}

static int32_t video_out_close(int32_t handle)
{
    pthread_mutex_lock(&ports_lock);
    port_of("sceVideoOutClose", handle)->open = 0;
    pthread_mutex_unlock(&ports_lock);
    return 0;
}

static int32_t set_flip_rate(int32_t handle, int32_t rate)
{
    static const char fn[] = "sceVideoOutSetFlipRate";
    if (rate != FLIP_RATE_60HZ && rate != FLIP_RATE_30HZ && rate != FLIP_RATE_20HZ) rt_fatal(fn, "a flip rate that is not established");
    pthread_mutex_lock(&ports_lock);
    port_of(fn, handle)->flip_rate = rate;
    pthread_mutex_unlock(&ports_lock);
    return 0;
}

static int32_t add_flip_event(rt_equeue *queue, int32_t handle, void *udata)
{
    static const char fn[] = "sceVideoOutAddFlipEvent";
    if (!queue) rt_fatal(fn, "no queue: the error value is not established");
    pthread_mutex_lock(&ports_lock);
    video_port *const p = port_of(fn, handle);
    if (p->queue_count == MAX_FLIP_QUEUES) rt_fatal(fn, "more flip queues than the runtime holds");
    if (rt_equeue_add(queue, RT_EVFILT_DISPLAY, 0, udata)) rt_fatal(fn, "the queue is full");
    p->queues[p->queue_count++] = queue;
    pthread_mutex_unlock(&ports_lock);
    return 0;
}

/* The public API has no result. The libSceVideoOut implementation also leaves
   zero-extended pitch in RAX. Values are copied without format validation. */
static uint64_t set_buffer_attribute(void *output, uint32_t format, uint32_t tiling, uint32_t aspect,
                                     uint32_t width, uint32_t height, uint32_t pitch)
{
    if (!output) rt_fatal("sceVideoOutSetBufferAttribute", "no attribute to fill: the error value is not established");
    const uint32_t captured_pitch = pitch;
    rt_video_out_buffer_attribute *attribute = output;
    memset(attribute->reserved, 0, sizeof attribute->reserved);
    const uint32_t fields[] = {format, tiling, aspect, width, height, captured_pitch};
    memcpy(attribute, fields, sizeof fields);
    return captured_pitch;
}

static int32_t register_buffers(int32_t handle, int32_t start, void *const *addresses, int32_t count,
                                const rt_video_out_buffer_attribute *attribute)
{
    static const char fn[] = "sceVideoOutRegisterBuffers";
    if (!addresses || !attribute || count < 1) rt_fatal(fn, "no buffers or attribute: the error value is not established");
    pthread_mutex_lock(&ports_lock);
    video_port *const p = port_of(fn, handle);
    if (start || p->buffer_count) rt_fatal(fn, "a second set or a start other than 0: not established");
    if (count > MAX_BUFFERS) rt_fatal(fn, "more buffers than the runtime holds");
    for (int i = 0; i < count; i++) {
        if (!addresses[i]) rt_fatal(fn, "a missing buffer address: the error value is not established");
        p->buffers[i] = addresses[i];
    }
    p->buffer_count = count;
    p->attribute = *attribute;
    pthread_mutex_unlock(&ports_lock);
    uint32_t width, height;
    memcpy(&width, attribute->width, sizeof width);
    memcpy(&height, attribute->height, sizeof height);
    rt_display_attach(handle, width, height, count); /* host frames for the renderer */
    return 0; /* the set's index: this is the first */
}

/* A vsync flip takes effect at the display's next vertical blank. The runtime has no display, so
   its vertical blanks are the 60 Hz ticks of the host's monotonic clock (flip rate 0, the only one
   it accepts): the flip waits on the calling thread for the next tick, then completes. */
static void wait_for_vblank(const char *function)
{
    const int64_t period_ns = 1000000000 / VBLANKS_PER_SECOND;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) rt_fatal(function, "the host monotonic clock read failed");
    const int64_t now_ns = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    const int64_t next_ns = (now_ns / period_ns + 1) * period_ns;
    const struct timespec at = {(time_t)(next_ns / 1000000000), (long)(next_ns % 1000000000)};
    if (rt_host_sleep_until_monotonic(&at)) rt_fatal(function, "the host monotonic wait failed");
}

int32_t rt_video_out_flip(const char *fn, int32_t handle, int32_t buffer, int32_t mode, int64_t argument)
{
    if (mode != FLIP_MODE_HSYNC && mode != FLIP_MODE_VSYNC) rt_fatal(fn, "a flip mode other than vsync (1) or hsync (2)");
    if (mode == FLIP_MODE_VSYNC) wait_for_vblank(fn);
    pthread_mutex_lock(&ports_lock);
    video_port *const p = port_of(fn, handle);
    if (buffer < 0 || buffer >= p->buffer_count) rt_fatal(fn, "a buffer index outside the registered set");
    p->status.count++;
    p->status.flip_argument = argument;
    p->status.current_buffer = buffer;
    const uint64_t address = (uint64_t)(uintptr_t)p->buffers[buffer];
    uint32_t width, height;
    memcpy(&width, p->attribute.width, sizeof width);
    memcpy(&height, p->attribute.height, sizeof height);
    rt_equeue *queues[MAX_FLIP_QUEUES];
    const int n = p->queue_count;
    memcpy(queues, p->queues, sizeof queues);
    pthread_mutex_unlock(&ports_lock);
    /* what the renderer drew into that buffer, if anything, then shown before the game hears the
       flip is done */
    uint32_t *const frame = rt_display_frame(handle, buffer);
    if (frame) rt_renderer_frame(address, frame, width, height);
    rt_display_present(handle, buffer);
    for (int i = 0; i < n; i++) rt_equeue_trigger(queues[i], RT_EVFILT_DISPLAY, 0, argument);
    return 0;
}

static int32_t get_flip_status(int32_t handle, flip_status *out)
{
    static const char fn[] = "sceVideoOutGetFlipStatus";
    if (!out) rt_fatal(fn, "no status to fill: the error value is not established");
    pthread_mutex_lock(&ports_lock);
    video_port *const p = port_of(fn, handle);
    *out = p->status; /* before the first flip: all zero, no flips yet (see the note above) */
    pthread_mutex_unlock(&ports_lock);
    return 0;
}

static int32_t submit_flip(int32_t handle, int32_t buffer, int32_t mode, int64_t argument)
{
    return rt_video_out_flip("sceVideoOutSubmitFlip", handle, buffer, mode, argument);
}

RT_SYSLIB("libSceVideoOut", sceVideoOutOpen, video_out_open);
RT_SYSLIB("libSceVideoOut", sceVideoOutClose, video_out_close);
RT_SYSLIB("libSceVideoOut", sceVideoOutSetFlipRate, set_flip_rate);
RT_SYSLIB("libSceVideoOut", sceVideoOutAddFlipEvent, add_flip_event);
RT_SYSLIB("libSceVideoOut", sceVideoOutSetBufferAttribute, set_buffer_attribute);
RT_SYSLIB("libSceVideoOut", sceVideoOutRegisterBuffers, register_buffers);
RT_SYSLIB("libSceVideoOut", sceVideoOutSubmitFlip, submit_flip);
RT_SYSLIB("libSceVideoOut", sceVideoOutGetFlipStatus, get_flip_status);
