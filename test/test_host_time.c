/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/host_time.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned checks;
static volatile sig_atomic_t interrupts;
static void interrupted(int signal) { (void)signal; interrupts++; }
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
    struct itimerval timer = {{0, 1000}, {0, 1000}};
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    struct timespec deadline, after;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    CHECK(rt_host_sleep_until_monotonic(&deadline) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    timer = (struct itimerval){{0, 0}, {0, 0}}; CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    CHECK(sigaction(SIGALRM, &previous, NULL) == 0);
    CHECK(!before(after, deadline)); CHECK(interrupts > 0);
    printf("host deadline: %u checks, interrupted=%d, passed\n", checks, (int)interrupts);
    return 0;
}
