/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe the production worker's clear boundary before native pthread/TLV teardown hides it. */
#include "runtime/guest_pointer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
static void observed_clear(void);
#define rt_guest_pointer_clear observed_clear
#include "../runtime/syslib/kernel_thread.c"
#undef rt_guest_pointer_clear

static _Thread_local unsigned rounds;
static int32_t key;
static int32_t (*set_value)(int32_t,const void *);
static atomic_uint clears,destructors;
static unsigned checks,failures;
#define CHECK(value) do {++checks;if (!(value)) {++failures;fprintf(stderr,"cleanup check %d: %s\n",__LINE__,#value);}} while (0)
static uintptr_t direct(uint32_t at)
{
    uintptr_t value;__asm__ volatile("movq %%gs:(%1), %0":"=r"(value):"r"((uintptr_t)at):"memory");
    return value;
}
static void observed_clear(void)
{
    if (rounds!=2) {fprintf(stderr,"cleanup check: guest destructors must precede clear\n");_exit(1);}
    const uint32_t at=rt_guest_pointer_offset();
    if (direct(at)!=(uintptr_t)current->tls) {_exit(1);}
    const uintptr_t host=direct(0);
    rt_guest_pointer_clear();
    if (direct(at)!=0 || direct(0)!=host) {fprintf(stderr,"cleanup check: clear changed host or retained guest TCB\n");_exit(1);}
    atomic_fetch_add(&clears,1);
}
static void destructor(void *value)
{
    if (direct(rt_guest_pointer_offset())!=(uintptr_t)current->tls) _exit(1);
    atomic_fetch_add(&destructors,1);
    if (++rounds==1 && set_value(key,value)) _exit(1);
}
static void *worker(void *value)
{
    if (set_value(key,value)) _exit(1);
    if ((uintptr_t)value==2) posix_thread_exit(value);
    return value;
}
static void *find(const char *symbol)
{
    for (const rt_syslib_entry *e=rt_syslib_begin();e && e!=rt_syslib_end();++e)
        if (!strcmp(e->library,"libkernel") && !strcmp(e->symbol,symbol)) return e->fn;
    abort();
}
int main(void)
{
    alarm(10);
    int32_t (*create_key)(int32_t *,void (*)(void *))=find("scePthreadKeyCreate");
    int32_t (*delete_key)(int32_t)=find("scePthreadKeyDelete");set_value=find("scePthreadSetspecific");
    CHECK(create_key(&key,destructor)==0);
    rt_thread_register_main(NULL,0);guest_attr *attr=NULL;
    CHECK(attr_init(&attr)==0 && attr_set_stack_size(&attr,0x10000)==0 && attr_set_detach_state(&attr,0)==0);
    guest_thread *threads[2]={NULL,NULL};
    for (unsigned i=0;i<2;++i) CHECK(create_named_thread("cleanup fixture",&threads[i],&attr,worker,(void *)(uintptr_t)(i+1),"cleanup")==0);
    for (unsigned i=0;i<2;++i) {
        void *value=NULL;CHECK(join_thread("cleanup join",threads[i],&value)==0 && value==(void *)(uintptr_t)(i+1));
    }
    CHECK(atomic_load(&clears)==2 && atomic_load(&destructors)==4);
    CHECK(delete_key(key)==0 && attr_destroy(&attr)==0);alarm(0);
    printf("guest worker clear order: %u checks, %u clears, %u destructors, %u failures\n",checks,atomic_load(&clears),atomic_load(&destructors),failures);
    return failures?1:0;
}
