/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real owned TSD with controlled public API failures; no host TCB writes. */
#include "runtime/guest_tls.h"
#include "runtime/guest_pointer.h"
#include "runtime/process.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int mode,bind_calls,clear_calls;
static rt_guest_tls_key *other;
static rt_guest_tls_result bad(void) {return (rt_guest_tls_result){RT_GUEST_TLS_HOST_ERROR,EIO};}
static rt_guest_tls_result observed_create(rt_guest_tls_key **out)
{
    if (mode==1) return bad();
    return rt_guest_tls_create(out);
}
static rt_guest_tls_result observed_candidate(const rt_guest_tls_key *owned,rt_guest_tls_candidate *out)
{
    if (mode==2) return bad();
    if (mode==7) {
        if (rt_guest_tls_create(&other).status!=RT_GUEST_TLS_OK) _exit(88);
        return rt_guest_tls_get_candidate(other,out); /* Another owned, unbound slot. */
    }
    return rt_guest_tls_get_candidate(owned,out);
}
static rt_guest_tls_result observed_bind(rt_guest_tls_key *owned,void *value)
{
    ++bind_calls;
    if ((mode==3 && bind_calls==1) || (mode==4 && bind_calls==2) || (mode==6 && bind_calls==3)) return bad();
    return rt_guest_tls_bind(owned,value);
}
static rt_guest_tls_result observed_clear(rt_guest_tls_key *owned)
{
    ++clear_calls;
    if ((mode==5 && clear_calls==1) || (mode==8 && clear_calls==2) || (mode==9 && clear_calls==1)) return bad();
    return rt_guest_tls_clear(owned);
}
static rt_guest_tls_result observed_read(const rt_guest_tls_key *owned,void **out)
{
    if (mode==9) return bad();
    rt_guest_tls_result r=rt_guest_tls_read(owned,out);
    if (mode==10 && r.status==RT_GUEST_TLS_OK) *out=NULL;
    return r;
}
#define rt_guest_tls_create observed_create
#define rt_guest_tls_get_candidate observed_candidate
#define rt_guest_tls_bind observed_bind
#define rt_guest_tls_clear observed_clear
#define rt_guest_tls_read observed_read
#include "../runtime/guest_pointer_darwin.c"
#undef rt_guest_tls_create
#undef rt_guest_tls_get_candidate
#undef rt_guest_tls_bind
#undef rt_guest_tls_clear
#undef rt_guest_tls_read

static uint64_t tcb[2];
static uintptr_t original_host;
static unsigned checks,failures;
static uintptr_t gs_zero(void)
{
    uintptr_t value;__asm__ volatile("movq %%gs:0, %0":"=r"(value));return value;
}
static void stopped(const char *caller,const char *message)
{
    if (strcmp(caller,"guest thread pointer") || gs_zero()!=original_host) _exit(87);
    void *value=NULL;if (key && rt_guest_tls_read(key,&value).status!=RT_GUEST_TLS_OK) _exit(87);
    if (mode==1 || mode==2) {
        if (qualified || initialized || strcmp(message,"cannot prepare an owned representable public pthread key")) _exit(87);
    } else if (mode==8) {
        if (!qualified || value!=tcb || strcmp(message,"cannot clear the public/direct guest TCB")) _exit(87);
    } else if (mode==9) {
        if (qualified || !value || strcmp(message,"cannot clear a failed public/direct qualification")) _exit(87);
    } else if (mode==11) {
        if (qualified || key || strcmp(message,"calling host thread has no qualified guest TCB")) _exit(87);
    } else if (mode==12) {
        if (qualified || key || strcmp(message,"no TCB to bind")) _exit(87);
    } else if (qualified || value || strcmp(message,"public/direct TSD qualification failed on this host thread")) _exit(87);
    _exit(86);
}
int main(void)
{
    alarm(10);
    for (mode=1;mode<=12;++mode) {
        const pid_t child=fork();if (child<0) abort();
        if (!child) {
            original_host=gs_zero();rt_fatal_observer=stopped;
            if (mode==11) (void)rt_guest_pointer_offset();
            else if (mode==12) rt_guest_pointer_bind(NULL);
            else {rt_guest_pointer_bind(tcb);if (mode==8) rt_guest_pointer_clear();}
            _exit(0); /* Returning from an expected stop is failure. */
        }
        int status=0;++checks;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=86) {
            ++failures;fprintf(stderr,"pointer failure mode %d: status=%d\n",mode,status);
        }
    }
    alarm(0);printf("guest pointer controlled failures: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
