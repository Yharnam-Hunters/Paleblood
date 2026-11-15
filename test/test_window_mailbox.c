/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/display/window_mailbox.h"
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(v) do {++checks;if (!(v)) {fprintf(stderr,"mailbox line%d: %s\n",__LINE__,#v);exit(1);}} while(0)
static void literal(rt_window_mailbox *box,uint8_t *source,const uint8_t *expected,uint32_t width,uint32_t height)
{
    const size_t bytes=(size_t)width*height*4;
    CHECK(rt_window_mailbox_publish(box,(const uint32_t *)source,width,height)==0);
    memset(source,0,bytes); /* Publication owns a copy, including source lifetime/row order. */
    rt_window_frame frame={0};CHECK(rt_window_mailbox_poll(box,&frame)==1);
    CHECK(frame.width==width && frame.height==height && frame.rgba && !memcmp(frame.rgba,expected,bytes));free(frame.rgba);
    CHECK(rt_window_mailbox_poll(box,&frame)==1 && frame.rgba==NULL);
}
typedef struct {rt_window_mailbox *box;int result,mask_preserved;} Open;
static void *open_worker(void *opaque)
{
    Open *open=opaque;sigset_t canary,before,after;
    if (sigemptyset(&canary) || sigaddset(&canary,SIGUSR2) || pthread_sigmask(SIG_BLOCK,&canary,NULL) || pthread_sigmask(SIG_SETMASK,NULL,&before)) abort();
    open->result=rt_window_mailbox_open(open->box,7,1);
    if (pthread_sigmask(SIG_SETMASK,NULL,&after)) abort();
    open->mask_preserved=1;
    for (unsigned signal=1;signal<sizeof(sigset_t)*8;++signal) if (sigismember(&before,signal)!=sigismember(&after,signal)) open->mask_preserved=0;
    return NULL;
}
static void open_case(int cancel)
{
    rt_window_mailbox *box=rt_window_mailbox_create();CHECK(box!=NULL);Open open={box,-2,0};pthread_t worker;
    CHECK(pthread_create(&worker,NULL,open_worker,&open)==0);
    rt_window_frame frame={0};
    while (!rt_window_mailbox_poll(box,&frame) || !frame.opening) sched_yield();
    CHECK(frame.open_width==7 && frame.open_height==1);
    if (cancel) rt_window_mailbox_cancel(box);
    else while (!rt_window_mailbox_opened(box)) sched_yield();
    CHECK(pthread_join(worker,NULL)==0 && open.result==(cancel?-1:0) && open.mask_preserved);
    rt_window_mailbox_destroy(box);
}
int main(void)
{
    rt_window_mailbox *box=rt_window_mailbox_create();CHECK(box!=NULL);
    _Alignas(uint32_t) uint8_t single[]={3,7,11,13};const uint8_t one[]={11,7,3,13};literal(box,single,one,1,1);
    _Alignas(uint32_t) uint8_t row[]={1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16, 17,18,19,20, 21,22,23,24, 25,26,27,28};
    const uint8_t row_rgba[]={3,2,1,4, 7,6,5,8, 11,10,9,12, 15,14,13,16, 19,18,17,20, 23,22,21,24, 27,26,25,28};literal(box,row,row_rgba,7,1);
    _Alignas(uint32_t) uint8_t column[]={29,31,37,41, 43,47,53,59, 61,67,71,73, 79,83,89,97, 101,103,107,109};
    const uint8_t column_rgba[]={37,31,29,41, 53,47,43,59, 71,67,61,73, 89,83,79,97, 107,103,101,109};literal(box,column,column_rgba,1,5);
    _Alignas(uint32_t) uint8_t grid[]={2,4,6,8, 10,12,14,16, 18,20,22,24, 26,28,30,32};
    const uint8_t grid_rgba[]={6,4,2,8, 14,12,10,16, 22,20,18,24, 30,28,26,32};literal(box,grid,grid_rgba,2,2);
    const uint32_t old=0x04030201,newest=0x0c0b0a09;
    CHECK(rt_window_mailbox_publish(box,&old,1,1)==0 && rt_window_mailbox_publish(box,&newest,1,1)==0);
    const uint32_t invalid[]={0,4097,UINT32_MAX};
    for (size_t i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        CHECK(rt_window_mailbox_publish(box,&old,invalid[i],1)==-1);
        CHECK(rt_window_mailbox_publish(box,&old,1,invalid[i])==-1);
        CHECK(rt_window_mailbox_open(box,invalid[i],1)==-1);
    }
    CHECK(rt_window_mailbox_publish(box,NULL,1,1)==-1);
    rt_window_frame frame={0};CHECK(rt_window_mailbox_poll(box,&frame)==1 && frame.width==1 && frame.height==1);
    const uint8_t latest[]={11,10,9,12};CHECK(frame.rgba && !memcmp(frame.rgba,latest,4));free(frame.rgba);
    rt_window_mailbox_cancel(box);CHECK(rt_window_mailbox_publish(box,&old,1,1)==-1 && rt_window_mailbox_open(box,1,1)==-1);
    rt_window_mailbox_destroy(box);open_case(0);open_case(1);
    printf("window mailbox: %u checks, 0 failures\n",checks);return 0;
}
