/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Call production providers through their registered guest interfaces. */
#include "syslib_test.h"
#include "runtime/syslib.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
int failures;
static syslib_test *tests;
static unsigned extra_checks;
#define EXPECT(value) do { ++extra_checks; CHECK(value); } while (0)
void syslib_test_register(syslib_test *test) { test->next = tests; tests = test; }
void *fn(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); ++e)
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol)) return e->fn;
    fprintf(stderr,"missing %s:%s\n",library,symbol); ++failures; return NULL;
}
int is_implemented(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); ++e)
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol)) return e->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED;
    return 0;
}
typedef int (*Init)(void **,void **,const char *);
typedef int (*Lock)(void **);
typedef int (*Timed)(void **,uint32_t);
typedef int (*AttrType)(void **,int);
typedef struct {int64_t seconds,nanoseconds;} GuestTime;
typedef int (*AbsoluteWait)(void **,void **,const GuestTime *);
typedef int (*Wait)(void **,void **,uint32_t);
static Lock lock_mutex,unlock_mutex,signal_cond;
static void *mutex,*cond;
static atomic_int notified;
static unsigned signal_delay_us;
static void *signal_worker(void *unused)
{
    (void)unused;
    if (signal_delay_us) {struct timespec delay={0,(long)signal_delay_us*1000};nanosleep(&delay,NULL);}
    if (lock_mutex(&mutex)) abort();
    atomic_store(&notified,1);
    if (signal_cond(&cond) || unlock_mutex(&mutex)) abort();
    return NULL;
}
static int64_t monotonic_ns(void)
{
    struct timespec now; if (clock_gettime(CLOCK_MONOTONIC,&now)) abort();
    return (int64_t)now.tv_sec*1000000000+now.tv_nsec;
}
static void conditions(void)
{
    Init init_mutex=(Init)fn("libkernel","scePthreadMutexInit"),init_cond=(Init)fn("libkernel","scePthreadCondInit");
    Lock destroy_mutex=(Lock)fn("libkernel","scePthreadMutexDestroy"),destroy_cond=(Lock)fn("libkernel","scePthreadCondDestroy");
    Lock init_attr=(Lock)fn("libkernel","scePthreadMutexattrInit"),destroy_attr=(Lock)fn("libkernel","scePthreadMutexattrDestroy");
    AttrType attr_type=(AttrType)fn("libkernel","scePthreadMutexattrSettype");
    Timed timed_mutex=(Timed)fn("libkernel","scePthreadMutexTimedlock");
    Wait timed_cond=(Wait)fn("libkernel","scePthreadCondTimedwait");
    AbsoluteWait absolute=(AbsoluteWait)fn("libScePosix","pthread_cond_timedwait");
    lock_mutex=(Lock)fn("libkernel","scePthreadMutexLock");unlock_mutex=(Lock)fn("libkernel","scePthreadMutexUnlock");
    signal_cond=(Lock)fn("libkernel","scePthreadCondSignal");
    if (failures) return;
    for (unsigned recursive=0;recursive<2;++recursive) {
        void *attr=NULL;mutex=cond=NULL;
        EXPECT(init_attr(&attr)==0);EXPECT(attr_type(&attr,recursive?2:1)==0);
        EXPECT(init_mutex(&mutex,&attr,"condition")==0);EXPECT(destroy_attr(&attr)==0);EXPECT(init_cond(&cond,NULL,"condition")==0);
        EXPECT(timed_cond(&cond,&mutex,0)==(int)0x80020001u); /* EPERM even for an expired wait. */
        EXPECT(timed_mutex(&mutex,0)==0);
        if (recursive) EXPECT(lock_mutex(&mutex)==0);
        const int64_t start=monotonic_ns();
        EXPECT(timed_cond(&cond,&mutex,20000)==(int)0x8002003cu);
        EXPECT(monotonic_ns()-start>=20000000);
        EXPECT(timed_cond(&cond,&mutex,0)==(int)0x8002003cu);
        GuestTime expired={0,0},invalid={0,1000000000};
        EXPECT(absolute(&cond,&mutex,&expired)==60);EXPECT(absolute(&cond,&mutex,&invalid)==22);
        struct timespec wall;EXPECT(clock_gettime(CLOCK_REALTIME,&wall)==0);
        GuestTime future={wall.tv_sec,wall.tv_nsec+20000000};
        if (future.nanoseconds>=1000000000) {++future.seconds;future.nanoseconds-=1000000000;}
        const int64_t wall_start=monotonic_ns();EXPECT(absolute(&cond,&mutex,&future)==60);
        EXPECT(monotonic_ns()-wall_start>=19000000);
        /* A representable future guest deadline must not wrap Darwin's internal
           relative nanosecond interval into a timeout before the worker signals. */
        EXPECT(clock_gettime(CLOCK_REALTIME,&wall)==0);
        GuestTime distant={wall.tv_sec+(INT64_C(1)<<55),wall.tv_nsec+2000000};
        if (distant.nanoseconds>=1000000000) {++distant.seconds;distant.nanoseconds-=1000000000;}
        pthread_t worker;atomic_store(&notified,0);signal_delay_us=10000;
        EXPECT(pthread_create(&worker,NULL,signal_worker,NULL)==0);
        EXPECT(absolute(&cond,&mutex,&distant)==0);EXPECT(atomic_load(&notified)==1);
        EXPECT(unlock_mutex(&mutex)==0);if (recursive) EXPECT(unlock_mutex(&mutex)==0);
        EXPECT(pthread_join(worker,NULL)==0);signal_delay_us=0;
        EXPECT(lock_mutex(&mutex)==0);if (recursive) EXPECT(lock_mutex(&mutex)==0);
        atomic_store(&notified,0);EXPECT(pthread_create(&worker,NULL,signal_worker,NULL)==0);
        while (!atomic_load(&notified)) EXPECT(timed_cond(&cond,&mutex,1000000)==0);
        EXPECT(pthread_join(worker,NULL)==0);
        EXPECT(unlock_mutex(&mutex)==0);if (recursive) EXPECT(unlock_mutex(&mutex)==0);
        EXPECT(unlock_mutex(&mutex)==(int)0x80020001u);
        EXPECT(destroy_cond(&cond)==0);EXPECT(destroy_mutex(&mutex)==0);
    }
}
int main(void)
{
    alarm(8);unsigned suites=0;
    for (syslib_test *test=tests;test;test=test->next) {test->run();++suites;}
    conditions();alarm(0);
    printf("syslib locks: %u existing suites, %u extra checks, %d failures\n",suites,extra_checks,failures);
    return failures?1:0;
}
