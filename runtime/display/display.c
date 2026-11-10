/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The host display: frames for the guest's frame buffers, and showing them (runtime/display.h).
   The frames live in host memory here; the window back end (display_vulkan.c) copies a presented
   frame to its window. */
#include "runtime/display.h"
#include "runtime/display_window.h"
#include "runtime/process.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_PORTS = 4, MAX_FRAMES = 16 };
enum { BYTES_PER_PIXEL = 4, PPM_MAX_CHANNEL = 255 };

typedef struct {
    uint32_t width, height;
    int count;
    uint32_t *frames[MAX_FRAMES];
    uint64_t presented;
} display_port;

static pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
static rt_display_mode display_mode = RT_DISPLAY_NONE;
static const char *capture_dir;
static display_port ports[MAX_PORTS + 1]; /* video out's handles count from 1 */
static int window_open;

int rt_display_configure(rt_display_mode mode, const char *capture)
{
    if (mode == RT_DISPLAY_WINDOW && !rt_display_window_available()) return -1;
    display_mode = mode;
    capture_dir = capture;
    return 0;
}

static display_port *port_of(const char *caller, int port)
{
    if (port < 1 || port > MAX_PORTS) rt_fatal(caller, "a video out port the display does not hold");
    return &ports[port];
}

void rt_display_attach(int port, uint32_t width, uint32_t height, int count)
{
    static const char fn[] = "rt_display_attach";
    if (display_mode == RT_DISPLAY_NONE) return;
    if (!width || !height || count < 1 || count > MAX_FRAMES) rt_fatal(fn, "frame buffers the display cannot hold");
    pthread_mutex_lock(&display_lock);
    display_port *const p = port_of(fn, port);
    if (p->count) rt_fatal(fn, "a second set of frame buffers on one port");
    for (int i = 0; i < count; i++) {
        p->frames[i] = calloc((size_t)width * height, BYTES_PER_PIXEL); /* black until drawn */
        if (!p->frames[i]) rt_fatal(fn, "the host has no memory for a frame");
    }
    p->width = width, p->height = height, p->count = count;
    if (display_mode == RT_DISPLAY_WINDOW && !window_open) {
        if (rt_display_window_open(width, height)) rt_fatal(fn, "the host could not open the window");
        window_open = 1;
    }
    pthread_mutex_unlock(&display_lock);
}

uint32_t *rt_display_frame(int port, int buffer)
{
    if (display_mode == RT_DISPLAY_NONE) return NULL;
    pthread_mutex_lock(&display_lock);
    const display_port *const p = port_of("rt_display_frame", port);
    uint32_t *const frame = buffer >= 0 && buffer < p->count ? p->frames[buffer] : NULL;
    pthread_mutex_unlock(&display_lock);
    return frame;
}

/* frame-NNNNNN.ppm, numbered from 0 in presentation order: binary PPM (P6), the frame's red, green
   and blue, written a row at a time. */
static void write_ppm(const char *path, const uint32_t *frame, uint32_t width, uint32_t height)
{
    static const char fn[] = "rt_display_present";
    FILE *f = fopen(path, "wb");
    if (!f) rt_fatal(fn, "the host could not write a captured frame");
    unsigned char *row = malloc((size_t)width * 3);
    if (!row) rt_fatal(fn, "the host has no memory to capture a frame");
    fprintf(f, "P6\n%u %u\n%d\n", width, height, PPM_MAX_CHANNEL);
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *pixel = frame + (size_t)y * width;
        for (uint32_t x = 0; x < width; x++) {
            row[x * 3 + 0] = (unsigned char)(pixel[x] >> 16); /* red */
            row[x * 3 + 1] = (unsigned char)(pixel[x] >> 8);  /* green */
            row[x * 3 + 2] = (unsigned char)pixel[x];         /* blue */
        }
        fwrite(row, 1, (size_t)width * 3, f);
    }
    free(row);
    fclose(f);
}

/* Captured frames are written by a thread of the display's own. A flip only copies the frame into
   a free slot, so neither the flip (and the flip event the game waits for) nor a pad read waits on
   a file being written: on a PS4 neither does any I/O, and the game's main and render threads share
   its graphics context with only timing margins between them, which a 6 MB write per frame used to
   eat (the opening movie then broke its command buffers in most captured runs). The flip waits only
   when every slot is still being written. */
enum { CAPTURE_SLOTS = 4 };
typedef struct {
    uint32_t *pixels;
    size_t capacity;  /* pixels the buffer holds */
    uint32_t width, height;
    char path[4096];
} capture_slot;

static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t capture_changed = PTHREAD_COND_INITIALIZER;
static capture_slot capture_slots[CAPTURE_SLOTS];
static unsigned capture_head, capture_count; /* the oldest queued slot, and how many are queued */
static int capture_writing;                  /* the writer holds a slot it took off the queue */
static int capture_thread_started;

static void *capture_writer(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&capture_lock);
    for (;;) {
        while (!capture_count) pthread_cond_wait(&capture_changed, &capture_lock);
        capture_slot *const slot = &capture_slots[capture_head];
        capture_writing = 1;
        pthread_mutex_unlock(&capture_lock);
        write_ppm(slot->path, slot->pixels, slot->width, slot->height);
        pthread_mutex_lock(&capture_lock);
        capture_head = (capture_head + 1) % CAPTURE_SLOTS;
        capture_count--;
        capture_writing = 0;
        pthread_cond_broadcast(&capture_changed);
    }
    return NULL;
}

/* Queues a copy of the frame for writing as capture frame `number`. */
static void capture_frame(const display_port *p, const uint32_t *frame, uint64_t number)
{
    static const char fn[] = "rt_display_present";
    const size_t pixels = (size_t)p->width * p->height;
    pthread_mutex_lock(&capture_lock);
    if (!capture_thread_started) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, capture_writer, NULL)) rt_fatal(fn, "the host could not start the capture writer");
        pthread_detach(thread);
        capture_thread_started = 1;
    }
    while (capture_count == CAPTURE_SLOTS) pthread_cond_wait(&capture_changed, &capture_lock);
    capture_slot *const slot = &capture_slots[(capture_head + capture_count) % CAPTURE_SLOTS];
    pthread_mutex_unlock(&capture_lock); /* a free slot is the producer's alone until queued */
    if (slot->capacity < pixels) {
        free(slot->pixels);
        slot->pixels = malloc(pixels * BYTES_PER_PIXEL);
        if (!slot->pixels) rt_fatal(fn, "the host has no memory to capture a frame");
        slot->capacity = pixels;
    }
    memcpy(slot->pixels, frame, pixels * BYTES_PER_PIXEL);
    slot->width = p->width, slot->height = p->height;
    snprintf(slot->path, sizeof slot->path, "%s/frame-%06llu.ppm", capture_dir, (unsigned long long)number);
    pthread_mutex_lock(&capture_lock);
    capture_count++;
    pthread_cond_broadcast(&capture_changed);
    pthread_mutex_unlock(&capture_lock);
}

void rt_display_capture_wait(void)
{
    pthread_mutex_lock(&capture_lock);
    while (capture_count) pthread_cond_wait(&capture_changed, &capture_lock);
    pthread_mutex_unlock(&capture_lock);
}

void rt_display_present(int port, int buffer)
{
    static const char fn[] = "rt_display_present";
    if (display_mode == RT_DISPLAY_NONE) return;
    pthread_mutex_lock(&display_lock);
    display_port *const p = port_of(fn, port);
    if (buffer < 0 || buffer >= p->count) rt_fatal(fn, "a buffer the port did not attach");
    const uint32_t *const frame = p->frames[buffer];
    if (capture_dir) capture_frame(p, frame, p->presented);
    __atomic_store_n(&p->presented, p->presented + 1, __ATOMIC_RELEASE);
    if (display_mode == RT_DISPLAY_WINDOW) rt_display_window_show(frame, p->width, p->height);
    pthread_mutex_unlock(&display_lock);
}

/* Read without the display's lock, so a pad read (the input script counts presented frames) never
   waits on a flip in progress. */
uint64_t rt_display_presented(int port)
{
    return port >= 1 && port <= MAX_PORTS ? __atomic_load_n(&ports[port].presented, __ATOMIC_ACQUIRE) : 0;
}
