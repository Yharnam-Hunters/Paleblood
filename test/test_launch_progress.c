/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Launch progress (runtime/launch_progress.h): counts told from several workers reach the listener in
   order whatever order the workers get to tell them in. Worker A counts 1 of 2 and pauses; worker B
   counts 2 of 2 and tells it; then A tells its 1: the listener must still end on 2 of 2 (the status
   cleared), never on A's stale 1. A count already told is not told again. */
#include "runtime/launch_progress.h"

#include <pthread.h>
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

/* The order the workers tell in: main lets one worker at a time go (turn) and waits until it has
   told (finished). A mutex and condition variables, which every host's threads have. */
static pthread_mutex_t order_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t turn_changed = PTHREAD_COND_INITIALIZER, worker_finished = PTHREAD_COND_INITIALIZER;
static int turn, finished; /* the worker let go (0: none yet); how many workers have told */

typedef struct {
    launch_progress *progress;
    int name;    /* the turn it waits for */
    size_t done; /* the count this worker got from its increment */
    int told;
} worker;

static void *tell_when_let(void *argument)
{
    worker *w = argument;
    pthread_mutex_lock(&order_lock);
    while (turn != w->name) pthread_cond_wait(&turn_changed, &order_lock);
    pthread_mutex_unlock(&order_lock);
    w->told = launch_progress_publish(w->progress, w->done, listen, NULL);
    pthread_mutex_lock(&order_lock);
    finished++;
    pthread_cond_signal(&worker_finished);
    pthread_mutex_unlock(&order_lock);
    return NULL;
}

/* Lets worker name tell, and waits until it has. */
static void let_tell(int name)
{
    pthread_mutex_lock(&order_lock);
    const int before = finished;
    turn = name;
    pthread_cond_broadcast(&turn_changed);
    while (finished == before) pthread_cond_wait(&worker_finished, &order_lock);
    pthread_mutex_unlock(&order_lock);
}

int main(void)
{
    launch_progress progress;
    if (launch_progress_init(&progress)) {
        fprintf(stderr, "launch progress: its lock could not be made\n");
        return 1;
    }
    CHECK(launch_progress_publish(&progress, 0, listen, NULL)); /* 0 of COUNT as the workers start */
    worker a = {&progress, 'A', 1, -1}, b = {&progress, 'B', COUNT, -1};
    pthread_t threads[2];
    const int started_a = pthread_create(&threads[0], NULL, tell_when_let, &a) == 0;
    const int started_b = pthread_create(&threads[1], NULL, tell_when_let, &b) == 0;
    CHECK(started_a && started_b);
    if (!started_a || !started_b) return 1;
    let_tell('B'); /* B, which counted last, tells first */
    let_tell('A'); /* then A's stale count */
    pthread_join(threads[0], NULL), pthread_join(threads[1], NULL);
    CHECK(b.told == 1 && a.told == 0);
    CHECK(heard_count == 2 && heard[0] == 0 && heard[1] == COUNT);
    CHECK(heard_count > 0 && heard[heard_count - 1] == COUNT); /* the last thing heard: done */
    CHECK(!launch_progress_publish(&progress, COUNT, listen, NULL)); /* nothing told twice */
    launch_progress_destroy(&progress);
    if (failures) fprintf(stderr, "launch progress: %d failures\n", failures);
    return failures != 0;
}
