/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/host_time.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sched.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned checks;
static volatile sig_atomic_t interrupts;
static void interrupted(int signal) { (void)signal; interrupts++; }
/* A joined sender has an observable end: no new signal request can race handler restoration.
   The former process-timer fixture still died from SIGALRM after disarm/drain in Intel CI. */
typedef struct {
    pthread_t owner;
    atomic_int stop, ready, error;
} interrupt_sender;
static void *send_interrupts(void *opaque)
{
    interrupt_sender *sender=opaque;
    do {
        const int result=pthread_kill(sender->owner,SIGALRM);
        if (result) atomic_store(&sender->error,result);
        atomic_store(&sender->ready,1);
        if (result) return NULL;
        struct timespec remaining={0,1000000};
        while (nanosleep(&remaining,&remaining) && errno==EINTR) {}
    } while (!atomic_load(&sender->stop));
    return NULL;
}
static int before(struct timespec a, struct timespec b) { return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec); }
int main(void)
{
    CHECK(rt_host_sleep_until_monotonic(NULL) == EINVAL);
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){1, 1000000000}) == EINVAL);
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){-1, 0}) == EINVAL);
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){0, 0}) == 0);
    struct sigaction action = {0}, previous;
    action.sa_handler = interrupted; sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGALRM, &action, &previous) == 0);
    sigset_t alarms, old_mask, pending;
    CHECK(sigemptyset(&alarms) == 0 && sigaddset(&alarms, SIGALRM) == 0);
    CHECK(pthread_sigmask(SIG_BLOCK, &alarms, &old_mask) == 0);
    interrupt_sender sender={.owner=pthread_self()};
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,send_interrupts,&sender) == 0);
    while (!atomic_load(&sender.ready)) sched_yield();
    CHECK(atomic_load(&sender.error) == 0);
    CHECK(pthread_sigmask(SIG_SETMASK, &old_mask, NULL) == 0);
    struct timespec deadline, after;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    CHECK(rt_host_sleep_until_monotonic(&deadline) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    /* Stop and join the only sender while its target blocks the signal. Only after join
       can pending notifications be drained without another request racing restoration. */
    CHECK(pthread_sigmask(SIG_BLOCK, &alarms, NULL) == 0);
    atomic_store(&sender.stop,1);
    CHECK(pthread_join(thread,NULL) == 0);
    CHECK(atomic_load(&sender.error) == 0);
    for (;;) {
        CHECK(sigpending(&pending) == 0);
        const int waiting = sigismember(&pending, SIGALRM);
        CHECK(waiting >= 0);
        if (!waiting) break;
        int received = 0;
        CHECK(sigwait(&alarms, &received) == 0 && received == SIGALRM);
    }
    CHECK(sigaction(SIGALRM, &previous, NULL) == 0);
    CHECK(pthread_sigmask(SIG_SETMASK, &old_mask, NULL) == 0);
    CHECK(!before(after, deadline)); CHECK(interrupts > 0);
    printf("host deadline: %u checks, interrupted=%d, passed\n", checks, (int)interrupts);
    return 0;
}
