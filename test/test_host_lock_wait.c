/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#define clock_gettime fake_clock_gettime
#include "runtime/host_lock_wait.h"
#include <stdint.h>
#include <stdio.h>
static unsigned checks,failures,attempts,sleeps;
static int clock_error,sleep_error,succeed_at,attempt_error;
static struct timespec now,last_wake;
#define CHECK(value) do {++checks;if (!(value)){++failures;fprintf(stderr,"%d: %s\n",__LINE__,#value);}} while (0)
int fake_clock_gettime(clockid_t clock, struct timespec *out)
{
    CHECK(clock==CLOCK_MONOTONIC);
    if (clock_error) {errno=clock_error;return -1;}
    *out=now;return 0;
}
int rt_host_sleep_until_monotonic(const struct timespec *wake)
{
    CHECK(wake->tv_nsec>=0 && wake->tv_nsec<1000000000);
    last_wake=*wake;++sleeps;if (sleep_error) return sleep_error;
    now=*wake;return 0;
}
static int attempt(void *lock)
{
    CHECK(lock==&attempts);++attempts;
    return attempt_error?attempt_error:(succeed_at>0 && attempts>=(unsigned)succeed_at?0:EBUSY);
}
static int wait_until(struct timespec deadline) {return rt_host_lock_wait(attempt,&attempts,&deadline);}
static void reset(void)
{
    now=last_wake=(struct timespec){0,0};attempts=sleeps=0;clock_error=sleep_error=attempt_error=0;succeed_at=0;
}
int main(void)
{
    reset();succeed_at=1;CHECK(wait_until((struct timespec){0,0})==0 && attempts==1 && !sleeps);
    reset();CHECK(wait_until((struct timespec){0,0})==ETIMEDOUT && attempts==1 && !sleeps);
    reset();succeed_at=3;CHECK(wait_until((struct timespec){0,10000000})==0 && attempts==3 && sleeps==2 && now.tv_nsec==2000000);
    reset();CHECK(wait_until((struct timespec){0,1500000})==ETIMEDOUT && sleeps==2 && last_wake.tv_nsec==1500000);
    reset();now=(struct timespec){1,999500000};CHECK(wait_until((struct timespec){2,700000})==ETIMEDOUT && sleeps==2 && last_wake.tv_sec==2 && last_wake.tv_nsec==700000);
    reset();now=(struct timespec){INT64_MAX-1,999500000};CHECK(wait_until((struct timespec){INT64_MAX,700000})==ETIMEDOUT && sleeps==2 && last_wake.tv_sec==INT64_MAX);
    reset();clock_error=EIO;CHECK(wait_until((struct timespec){1,0})==EIO && attempts==1 && !sleeps);
    reset();sleep_error=EIO;CHECK(wait_until((struct timespec){1,0})==EIO && attempts==1 && sleeps==1);
    reset();attempt_error=EAGAIN;CHECK(wait_until((struct timespec){1,0})==EAGAIN && attempts==1 && !sleeps);
    reset();CHECK(wait_until((struct timespec){-1,0})==EINVAL && !attempts);
    CHECK(wait_until((struct timespec){0,-1})==EINVAL && !attempts);CHECK(wait_until((struct timespec){0,1000000000})==EINVAL && !attempts);
    CHECK(rt_host_lock_wait(attempt,&attempts,NULL)==EINVAL && !attempts);
    printf("host lock wait: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
