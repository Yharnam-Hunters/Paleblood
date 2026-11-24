/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Launch progress (runtime/launch_progress.h): counts told from several workers reach the listener in
   order whatever order the workers get to tell them in. Worker A counts 1 of 2 and pauses; worker B
   counts 2 of 2 and tells it; then A tells its 1: the listener must still end on 2 of 2 (the status
   cleared), never on A's stale 1. A count already told is not told again. */
#include "runtime/launch_progress.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { COUNT = 2 };

static size_t heard[8], heard_count;

static void listen(void *context, size_t done)
{
    (void)context;
    if (heard_count < sizeof heard / sizeof *heard) heard[heard_count++] = done;
}

typedef struct {
    launch_progress *progress;
    size_t done;          /* the count this worker got from its increment */
    sem_t *go, *finished; /* waits for go, then tells, then posts finished */
    int told;
} worker;

static void *tell_when_let(void *argument)
{
    worker *w = argument;
    sem_wait(w->go);
    w->told = launch_progress_publish(w->progress, w->done, listen, NULL);
    sem_post(w->finished);
    return NULL;
}

int main(void)
{
    launch_progress progress;
    launch_progress_init(&progress);
    CHECK(launch_progress_publish(&progress, 0, listen, NULL)); /* 0 of COUNT as the workers start */
    sem_t go_a, go_b, finished;
    sem_init(&go_a, 0, 0), sem_init(&go_b, 0, 0), sem_init(&finished, 0, 0);
    worker a = {&progress, 1, &go_a, &finished, -1}, b = {&progress, COUNT, &go_b, &finished, -1};
    pthread_t threads[2];
    CHECK(pthread_create(&threads[0], NULL, tell_when_let, &a) == 0);
    CHECK(pthread_create(&threads[1], NULL, tell_when_let, &b) == 0);
    sem_post(&go_b), sem_wait(&finished); /* B, which counted last, tells first */
    sem_post(&go_a), sem_wait(&finished); /* then A's stale count */
    pthread_join(threads[0], NULL), pthread_join(threads[1], NULL);
    CHECK(b.told == 1 && a.told == 0);
    CHECK(heard_count == 2 && heard[0] == 0 && heard[1] == COUNT);
    CHECK(heard_count > 0 && heard[heard_count - 1] == COUNT); /* the last thing heard: done */
    CHECK(!launch_progress_publish(&progress, COUNT, listen, NULL)); /* nothing told twice */
    launch_progress_destroy(&progress);
    sem_destroy(&go_a), sem_destroy(&go_b), sem_destroy(&finished);
    if (failures) fprintf(stderr, "launch progress: %d failures\n", failures);
    return failures != 0;
}
