/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "window_mailbox.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct rt_window_mailbox {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    atomic_int cancelled;
    int open_state;
    uint32_t open_width, open_height, width, height;
    uint8_t *rgba;
};
static int size_of(uint32_t width, uint32_t height, size_t *bytes)
{
    if (!width || !height || width>4096 || height>4096 || (size_t)width>SIZE_MAX/4/height) return -1;
    *bytes=(size_t)width*height*4;return 0;
}
/* Recovery must not abandon a held publisher lock. Restore only after unlocking. */
static int lock_publisher(rt_window_mailbox *box, sigset_t *previous)
{
    sigset_t owned;
    if (sigemptyset(&owned) || sigaddset(&owned,SIGALRM) || sigaddset(&owned,SIGUSR1) ||
        pthread_sigmask(SIG_BLOCK,&owned,previous)) return -1;
    if (!pthread_mutex_lock(&box->lock)) return 0;
    (void)pthread_sigmask(SIG_SETMASK,previous,NULL);return -1;
}
static int unlock_publisher(rt_window_mailbox *box, const sigset_t *previous)
{
    const int unlocked=pthread_mutex_unlock(&box->lock);
    const int restored=pthread_sigmask(SIG_SETMASK,previous,NULL);
    return unlocked || restored ? -1 : 0;
}
rt_window_mailbox *rt_window_mailbox_create(void)
{
    rt_window_mailbox *box=calloc(1,sizeof *box);
    if (!box) return NULL;
    atomic_init(&box->cancelled,0);
    if (pthread_mutex_init(&box->lock,NULL)) {free(box);return NULL;}
    if (pthread_cond_init(&box->changed,NULL)) {pthread_mutex_destroy(&box->lock);free(box);return NULL;}
    return box;
}
void rt_window_mailbox_destroy(rt_window_mailbox *box)
{
    if (!box) return;
    free(box->rgba);pthread_cond_destroy(&box->changed);pthread_mutex_destroy(&box->lock);free(box);
}
void rt_window_mailbox_cancel(rt_window_mailbox *box)
{
    if (box) atomic_store_explicit(&box->cancelled,1,memory_order_release);
}
int rt_window_mailbox_open(rt_window_mailbox *box, uint32_t width, uint32_t height)
{
    size_t bytes;sigset_t previous;
    if (!box || size_of(width,height,&bytes)) return -1;
    if (lock_publisher(box,&previous)) return -1;
    if (!box->open_state) {box->open_width=width;box->open_height=height;box->open_state=1;}
    if (unlock_publisher(box,&previous)) return -1;
    /* Five seconds total in at most 500 released timed slices. No main-thread wait/join. */
    for (unsigned slice=0;slice<500;++slice) {
        if (lock_publisher(box,&previous)) return -1;
        int state=box->open_state;
        const int cancelled=atomic_load_explicit(&box->cancelled,memory_order_acquire);
        int wait_error=0;
        if (state==1 && !cancelled) {
#if defined(__APPLE__)
            const struct timespec duration={0,10000000};
            const int result=pthread_cond_timedwait_relative_np(&box->changed,&box->lock,&duration);
            wait_error=result && result!=ETIMEDOUT;
            state=box->open_state;
#else
            struct timespec until;
            if (clock_gettime(CLOCK_REALTIME,&until)) wait_error=1;
            else {
                until.tv_nsec+=10000000;
                if (until.tv_nsec>=1000000000) {until.tv_nsec-=1000000000;++until.tv_sec;}
                const int result=pthread_cond_timedwait(&box->changed,&box->lock,&until);
                wait_error=result && result!=ETIMEDOUT;
                state=box->open_state;
            }
#endif
        }
        if (unlock_publisher(box,&previous)) return -1;
        if (cancelled || atomic_load_explicit(&box->cancelled,memory_order_acquire) || wait_error) return -1;
        if (state==2) return 0;
    }
    return -1;
}
int rt_window_mailbox_publish(rt_window_mailbox *box, const uint32_t *bgra, uint32_t width, uint32_t height)
{
    size_t bytes;sigset_t previous;
    if (!box || !bgra || size_of(width,height,&bytes) || atomic_load_explicit(&box->cancelled,memory_order_acquire)) return -1;
    uint8_t *rgba=malloc(bytes);
    if (!rgba) return -1;
    const uint8_t *source=(const uint8_t *)bgra;
    for (size_t at=0;at<bytes;at+=4) {
        rgba[at]=source[at+2];rgba[at+1]=source[at+1];rgba[at+2]=source[at];rgba[at+3]=source[at+3];
    }
    if (lock_publisher(box,&previous)) {free(rgba);return -1;}
    uint8_t *old=NULL;
    const int cancelled=atomic_load_explicit(&box->cancelled,memory_order_acquire);
    if (!cancelled) {old=box->rgba;box->rgba=rgba;box->width=width;box->height=height;}
    const int failed=unlock_publisher(box,&previous);
    free(cancelled ? rgba : old);return cancelled || failed ? -1 : 0;
}
int rt_window_mailbox_poll(rt_window_mailbox *box, rt_window_frame *frame)
{
    if (!box || !frame || pthread_mutex_trylock(&box->lock)) return 0;
    *frame=(rt_window_frame){box->rgba,box->width,box->height,box->open_width,box->open_height,box->open_state==1};
    box->rgba=NULL;
    pthread_mutex_unlock(&box->lock);return 1;
}
int rt_window_mailbox_opened(rt_window_mailbox *box)
{
    if (!box || pthread_mutex_trylock(&box->lock)) return 0;
    if (!atomic_load_explicit(&box->cancelled,memory_order_acquire)) box->open_state=2;
    pthread_cond_broadcast(&box->changed);pthread_mutex_unlock(&box->lock);return 1;
}
