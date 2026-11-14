/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production Darwin condition conversion with controlled native waits and clocks. */
#include "runtime/syslib.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks,failures,calls,clock_calls,fail_clock;
static int status[4];
static time_t elapsed[4];
static struct timespec received[4],mono;
#define CHECK(value) do {++checks;if (!(value)){++failures;fprintf(stderr,"%d: %s\n",__LINE__,#value);}} while (0)
int rt_test_clock_gettime(clockid_t clock,struct timespec *out)
{
    if (++clock_calls==fail_clock) {errno=EINVAL;return -1;}
    CHECK(clock==CLOCK_REALTIME || clock==CLOCK_MONOTONIC);
    *out=clock==CLOCK_REALTIME?(struct timespec){1000,0}:mono;return 0;
}
int rt_test_cond_wait(pthread_cond_t *cond,pthread_mutex_t *mutex,const struct timespec *remaining)
{
    (void)cond;(void)mutex;
    CHECK(calls<4);
    if (calls>=4) return EINVAL;
    CHECK(remaining->tv_sec>=0 && remaining->tv_sec<=3600);
    CHECK(remaining->tv_nsec>=0 && remaining->tv_nsec<1000000000);
    received[calls]=*remaining;mono.tv_sec+=elapsed[calls];return status[calls++];
}
static void *find(const char *library,const char *symbol)
{
    for (const rt_syslib_entry *e=rt_syslib_begin();e && e!=rt_syslib_end();++e)
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol)) return e->fn;
    abort();
}
static void reset(void)
{
    calls=clock_calls=fail_clock=0;mono=(struct timespec){100,0};
    memset(status,0,sizeof status);memset(elapsed,0,sizeof elapsed);memset(received,0,sizeof received);
}
int main(void)
{
    typedef int (*Lock)(void **);
    typedef struct {int64_t seconds,nanoseconds;} GuestTime;
    typedef int (*Wait)(void **,void **,const GuestTime *);
    Lock lock=(Lock)find("libkernel","scePthreadMutexLock"),unlock=(Lock)find("libkernel","scePthreadMutexUnlock");
    Lock destroy_m=(Lock)find("libkernel","scePthreadMutexDestroy"),destroy_c=(Lock)find("libkernel","scePthreadCondDestroy");
    Wait wait=(Wait)find("libScePosix","pthread_cond_timedwait");void *m=NULL,*c=NULL;CHECK(lock(&m)==0);
    reset();elapsed[0]=elapsed[1]=3600;elapsed[2]=1;status[0]=status[1]=status[2]=ETIMEDOUT;
    GuestTime deadline={8201,0};CHECK(wait(&c,&m,&deadline)==60);CHECK(calls==3);
    CHECK(received[0].tv_sec==3600 && received[1].tv_sec==3600 && received[2].tv_sec==1);
    reset();deadline=(GuestTime){4600,1};CHECK(wait(&c,&m,&deadline)==0);CHECK(calls==1 && received[0].tv_sec==3600 && received[0].tv_nsec==0);
    reset();deadline=(GuestTime){1000+(INT64_C(1)<<55),0};CHECK(wait(&c,&m,&deadline)==0);CHECK(calls==1 && received[0].tv_sec==3600);
    reset();elapsed[0]=3600;status[0]=ETIMEDOUT;deadline=(GuestTime){8201,0};
    CHECK(wait(&c,&m,&deadline)==0);CHECK(calls==2 && received[1].tv_sec==3600);
    reset();elapsed[0]=7202;status[0]=status[1]=ETIMEDOUT;
    CHECK(wait(&c,&m,&deadline)==60);CHECK(calls==2 && received[1].tv_sec==0 && received[1].tv_nsec==0);
    reset();fail_clock=3;CHECK(wait(&c,&m,&deadline)==22);CHECK(calls==0);
    CHECK(unlock(&m)==0);CHECK(destroy_m(&m)==0);CHECK(destroy_c(&c)==0);
    printf("host condition wait: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
