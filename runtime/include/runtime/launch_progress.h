/* SPDX-License-Identifier: GPL-2.0-or-later */
/* How far the shaders compiled at launch are, told in order (runtime/renderer/render_executor.c,
   compile_catalog; docs/RENDERER.md, "Compute shaders compiled while the game loads").

   The launch workers count each shader done with an atomic increment, but the counts they get are then
   told on their own threads, in whatever order they get there: a worker that counted 1 of 2 and paused
   could tell it after another worker told 2 of 2, and put back a "compiling" status after the last one
   cleared it. So every count is told through launch_progress_publish: under one lock, and only when it
   is past every count told before. The last count (done == count) is therefore never followed by
   another. */
#ifndef RUNTIME_LAUNCH_PROGRESS_H
#define RUNTIME_LAUNCH_PROGRESS_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    pthread_mutex_t lock;
    int told;         /* a count has been told */
    size_t last;      /* the last count told */
} launch_progress;

static inline void launch_progress_init(launch_progress *p)
{
    pthread_mutex_init(&p->lock, NULL);
    p->told = 0, p->last = 0;
}

static inline void launch_progress_destroy(launch_progress *p) { pthread_mutex_destroy(&p->lock); }

/* Calls tell(context, done) under the lock when done is the first count told or past the last one;
   returns whether it did. Any thread may call it. */
static inline int launch_progress_publish(launch_progress *p, size_t done, void (*tell)(void *context, size_t done), void *context)
{
    pthread_mutex_lock(&p->lock);
    const int newer = !p->told || done > p->last;
    if (newer) {
        p->told = 1, p->last = done;
        tell(context, done);
    }
    pthread_mutex_unlock(&p->lock);
    return newer;
}

#endif
